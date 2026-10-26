# AGENTS.md — `prompts/` (native prompt dispatcher, C++)

This directory builds the `prompt` command: a native C++ dispatcher where every
prompt is a `src/prompts/<name>.cpp` that registers itself. There is no scripting
runtime and no prompt-file scanning; the repo-wide guide is `../AGENTS.md`, and
this file is the working guide for this subtree.

## Build, test, lint

```bash
cmake -S prompts -B prompts/build -DCMAKE_BUILD_TYPE=Release && cmake --build prompts/build -j 8   # Release
cmake -S prompts -B prompts/build-asan -DCMAKE_BUILD_TYPE=Debug
cmake --build prompts/build-asan -j 8 && (cd prompts/build-asan && ctest)                          # Debug + tests
clang-format -i prompts/src/**/*.{cpp,hpp} && clang-format --dry-run -Werror prompts/src/**/*.{cpp,hpp}
clang-tidy -p prompts/build src/*.cpp src/*/*.cpp && clang-tidy -p prompts/build-asan tests/*.cpp  # zero findings
cmake --install prompts/build --prefix "$HOME/cmd" --component prompt                              # -> bin/prompt
```

The build type is **not** defaulted (only the `Debug` branch adds the warning
flags), so pass `-DCMAKE_BUILD_TYPE=Release` for the installed binary — a bare
`cmake -B prompts/build` yields an unoptimised no-config binary. `Debug` adds
`-g3 -Wall -Wpedantic -Wextra -Wconversion -Wshadow -fsanitize=address` and must
stay warning-free under **both GCC and clang**.

`enable_testing()`/`add_subdirectory(tests)` only run in a `Debug` build, so use
`build-asan` for `ctest`. `.clang-tidy` is treated as warnings-as-errors; the
whole tree must stay clang-format clean.

## Adding or changing a prompt

- **One file = one prompt.** A prompt is `src/prompts/<name>.cpp` implementing
  `static void execute_<name>(prompt::view const& v, prompt::result& r)` and
  `static void render_help_<name>(std::ostream&)`, ending with
  `bool const registered = prompt::register_prompt("<name>", "<summary>", &execute_<name>, &render_help_<name>);`.
  CMake globs `src/prompts/*.cpp`, so no other file changes; registration runs at
  static-init time, which is why `prompt_lib` must stay an OBJECT library.
  The registry is a fixed array of `prompt_registry_capacity` (128) slots; a
  build that registers more aborts at startup (`register_prompt` prints and
  calls `std::abort`) rather than silently dropping a prompt, and
  `test_chaining` asserts the catalog fits.
  A data prompt and its data-free lens may share one file when they belong
  together (`tse_intraday.cpp` registers `tse.intraday` and
  `tse.intraday.analysis`); give each its own `bool const` registration
  variable.
- `<summary>` is the first help paragraph verbatim; it is what `prompt list`
  prints. The help's first line must be `Usage: prompt <name>`.
- The name is passed explicitly because dotted/dashed names (`git.diff`,
  `tse.find`, `clang-tidy`) cannot be derived from the identifier.
- A **sink** uses `prompt::register_sink(...)` instead (see "Sinks" below). It
  needs only its executor, help renderer and stream flag; the dispatcher owns
  the shared flags and the barrier ordering.
- `record` (the last `register_prompt`/`register_sink` argument, default `true`)
  opts a prompt out of the history store; the `.history*` family sets it to
  `false`, since browsing the store must not append to it. `takes_body` sits just
  before it, so a non-body prompt that must not be recorded passes `false, false`.

## Prompt contract

Two types, one direction each: `prompt::view` is everything a segment may
**read**, `prompt::result` is everything it may **produce**. Nothing in a prompt
mutates chain state, which is what makes forked parallel segments safe.

- **Reading** (`sdk/stage.hpp`): `v.name`, `v.args` (a span — `.empty()`,
  `.size()`, indexed), `v.in.text()` (the shared stdin, a `string_view`),
  `v.env.git_root`/`v.env.exe_path`, `v.text` (the accumulated chain context,
  which a sink reads), `v.body` (the raw `.each` body tokens, empty for every
  prompt that is not registered with `takes_body`) and `v.prior` (the facts
  earlier segments declared or published). `v.stream` is the live-terminal
  writers, set by the dispatcher for a streaming sink only.
- **Writing** (`r.`): `r.out` (stdout — sanitised, piped onward, clipboarded),
  `r.err` (stderr, exit stays 0), `r.stderr_buffer` (sink streaming only),
  `r.published` (facts later segments read), `r.no_clipboard`, `r.clear`
  (sinks: drop the accumulated context after this sink succeeds).
- **Fail by throwing** (`src/sdk/errors.hpp`): `usage_error` (exit 2),
  `soft_error` (exit 1), or `prompt_error` with `message`, `partial_output`,
  `no_clipboard`, `stdin_consumed`. `main.cpp` catches them once; a failed
  prompt's partial stdout is still accumulated and the chain continues.
- **Stdin:** chained prompts share the **original** piped stdin, and it is
  spend-once: a prompt that reads it calls `v.in.spend()` (`v.in.text()`, then
  `v.in.spend()`, with `embed_stdin(v.in.text(), …)` formatting it).
  `v.in.piped()` says a pipe was attached at all; `v.in.available()` says nobody
  has read it yet.
- **Parallelism:** with no piped stdin the dispatcher runs independent segments
  in parallel (forked children; prompts must not assume they are the only
  segment running) and emits their results in argv order. Piped stdin keeps the
  chain sequential. A forked segment sees the **declarations** collected before
  the batch (that is what they are for) but never a sibling's `r.published`
  artifacts: those are folded back by the parent once the batch completes, and
  only later runs read them.

### Facts (`sdk/facts.hpp`)

Facts are the only inter-prompt channel: an ordered string→string store with
well-known keys in `namespace prompt::keys`. Values are length-prefixed on the
wire, so any content (newlines, UTF-8) survives. There are exactly two write
points, with two merge rules:

- **Declarations** — `register_prompt`'s last argument, a
  `void declare_<name>(declaration& d)`. They run in the parent *before* any
  batch forks and keep the **first** writer, so parallel siblings agree on
  identity derived purely from `d.args`/`d.env` (cheap: no I/O, no fzf, no
  network; a failing declaration is swallowed and the segment falls back to its
  own arguments).
- **Artifacts** — `r.published.set(key, value)` after a successful segment, with
  the **last** writer winning, i.e. the nearest successful preceding segment.
  That is exactly what `.save`'s filename wants.

One agreement rides on this today: `tse_data::declare_identifier` publishes
`keys::tse_symbol` once per chain, so `.tse foolad .tse.codal` and the reverse
order resolve the same instrument. `.delegate` and `.expect` publish
`keys::expect` (a newline-separated plan of `[NAME=]SELECTOR` entries) so
`.extract`, `.exec` and `.each` need no selector of their own; `.save` declares
`keys::save_wants_name` (a presence-only marker, with no target of its own) so
`.delegate` can ask the AI to name the file. `.files` needs no fact for a
sibling file prompt — its output
already carries the embedded files into the chain, and re-embedding them would
duplicate the content. Facts are deliberately opt-in: add a key when two prompts
genuinely cannot work it out from their own arguments, not as a general channel.

The fork wire (`sdk/wire.hpp`) carries a `segment_outcome` — stdout, stderr, the
separate stderr buffer, the published facts, the output flags and the exit
status — as one length-prefixed blob per child; `drain_all()` polls every child
pipe at once instead of draining them in turn.

## Prompt files (`prompt debug.prompt`)

`*.prompt` files hold the chain DSL that otherwise goes on the command line:
`prompt debug.prompt` behaves exactly as if the file's tokens had been typed
after `prompt`. `sdk/prompt_file.{hpp,cpp}` is a lexical pre-pass over argv that
runs in `main.cpp` before `parse_chain`, so chain parsing, sinks, `.each` bodies,
`.exec` ownership, the clipboard and the history store only ever see an ordinary
argv (the history records the *effective* tokens, which is what a replay wants).

- **Splicing:** any argument token ending in `.prompt` that resolves to a
  readable file is replaced, in place, by the file's tokens, so
  `prompt .note "context" debug.prompt .length short` works. A `*.prompt` token
  that does not resolve is an error only in the first argv position (where the
  `auto` fallback would silently eat it); elsewhere it stays an ordinary
  argument, so `.files missing.prompt` keeps the prompt's own warning. `--`
  stops the pass for the rest of that token sequence, which is what keeps
  `.exec -- command x.prompt` untouched, and `--no-prompt-files` as the first
  argument copies argv unchanged.
- **Tokenizer:** spaces and newlines separate; `#` at the start of a token
  comments out the rest of its line (`a#b` stays one token); `"..."` honours
  `\"` and `\\`, `'...'` is literal, and a backslash outside quotes escapes the
  next character. Nothing is expanded (no `$VAR`, globbing or command
  substitution). An unterminated quote is `prompt: <file>:<line>: unterminated "`
  and exits 2.
- **Resolution:** a name is looked up next to the file that named it (the
  current directory at the top level, so a nested `inner.prompt` means the one
  beside its includer), then `<git-root>/.prompt/`, then
  `$XDG_CONFIG_HOME/prompt/prompts` (`~/.config/prompt/prompts`). Files may
  include other files up to `kMaxPromptFileDepth` (8) deep; cycles are rejected
  on canonical paths and a file whose first token is `prompt` may keep it.
- The recursion (`expander::sequence` ↔ `expander::file`) is bounded by the
  depth cap and the cycle check, which is why both carry a
  `misc-no-recursion` suppression.

Tests: `tests/test_prompt_file.cpp` (tokenizer, inlining, nesting, cycle, depth,
lookup order, `--`, `--no-prompt-files`) and the `prompt_file_run`/
`prompt_file_missing` ctests over `tests/fixtures/hello.prompt`.

## Functions (`.func NAME(params) … .end`, called as `.name`)

A **function** is the third kind of step, next to a prompt and a sink: a named,
parameterized chain template the user defines and invokes exactly like a prompt.
`sdk/function.{hpp,cpp}` is a lexical pre-pass in `main.cpp` that runs
immediately after the `*.prompt` pass and before `parse_chain`, so sinks,
`.each` bodies, `.exec` ownership, the clipboard and the history store only ever
see an ordinary argv.

- **Definition.** `.func NAME(params) STEPS… .end`. The name and signature are a
  single token (`NAME` or `NAME(p, p=default)`, no spaces inside the parens), so
  a body may start with a bare token without ambiguity. The body ends at the
  matching `.end`, counting compiled body-taking steps (`.each` … `.end`) so
  theirs does not close the definition. Definitions may sit in the command, in a
  `*.prompt` file, or in a `funcs` file; several may appear in one source.
- **Invocation.** `.name args…` (a bare first token that names a function also
  works). A call binds declared params by position, or by `--name value` /
  `--name=value`; positionals past the declared params are `{@}`. In the body
  `{name}` is a value (embedded substitution inside a token is allowed), `{0}`
  is the function name and `{N}` the Nth argument. A `{placeholder}` that is not
  a declared param is left untouched, so `tse`/`.each` fields (`{symbol}`,
  `{item}`, …) compose. A function call's arguments stop at the next step, and
  the whole body is a normal chain.
- **Global sources.** `<git-root>/.prompt/funcs`, then
  `$XDG_CONFIG_HOME/prompt/funcs` (project over user per name). `$PROMPT_FUNCS`
  (colon-separated) replaces the list; an empty value disables global functions.
  A `funcs` file holds only `.func` blocks (a stray token is an error) and uses
  the same tokenizer as `*.prompt` files.
- **Errors are hard (exit 2).** A function may not shadow a compiled prompt or
  sink, nor use a reserved name (`func`, `end`, `prompt`); a duplicate in one
  source, a bad signature/name, a missing/unexpected `.end`, a missing required
  argument, an unknown `--param`, and recursion (direct or through another
  function, capped by `kMaxFunctionDepth`) are all failures. `.func` is top
  level only.
- **Composition with bodies.** A `.func` body may contain `.each … .end` and
  `.exec -- …`; the expander tracks compiled body depth and copies everything
  after `--` verbatim, and a function called inside an `.each` body is expanded
  by the parent so inline definitions stay visible to the child processes.

Tests: `tests/test_function.cpp` (definition/call, params/defaults/named flags/
`{@}`/`{0}`/`{N}`, nesting, `.each`-body and `--` preservation, every error
case, `funcs`-file loading and override) plus the
`function_inline`/`function_params`/`function_shadow` ctests. The suite's global
`ENVIRONMENT` sets `PROMPT_FUNCS=` so a developer's own funcs never leak in.

## Sinks

A sink is a chain barrier, not an ordinary prompt. Register it with
`prompt::register_sink("<name>", "<summary>", &execute_<name>, &render_help_<name>, nullptr, stream_output)`
(`stream_output=false` marks a silent sink) and no other switches; contiguous
sinks chain in argv order, each answer feeding the next. Adding a sink does not
touch the AI code or the dispatcher: the descriptor role makes `main.cpp` batch
everything before it, hand the sink the accumulated `v.text`, and let its answer
become the context later segments see.

- **Shared flags are dispatcher-owned** (`sdk/sink_types.hpp` + `sink.hpp`):
  `--on-error abort|continue|skip` (default abort), `--with-context`,
  `--show-context` and `--head N`. `parse_sink_args` removes them once in
  `main.cpp`; the sink implementation only sees its own arguments.
- **Reading the barrier:** the sink reads `v.text`; `--head` has already been
  applied to it by the dispatcher — do not trim it again (`trim_context_lines`
  is the shared helper behind it, applied before the redaction legend is
  prepended). A barrier's stderr is printed by the dispatcher and is never part
  of the chain context.
- **Streaming:** use `write_stdout(v, r, chunk)` and `write_stderr(v, r, chunk)`.
  They append to `r.out`/`r.stderr_buffer` and, when the dispatcher enabled
  streaming, call `v.stream.stdout_writer`/`v.stream.stderr_writer`
  immediately. `run_sink_tool(v, r, argv, stdin, timeout)` wires both to
  `process::command`'s `on_stdout`/`on_stderr` tee callbacks.
- **Terminal rendering:** a tool-backed sink's stdout is a pipe, so a tool that
  checks `isatty()` would drop its colour/Markdown. Pass its force-terminal flag
  (as `ai.*` does with `bin/ai --tty`) when `isatty(STDOUT_FILENO)`, keep the
  styled bytes display-only, and take the chain context from a plain side
  channel — `ai.*` reads `bin/ai --context-file` back into `r.out`, so rich
  frames never reach the clipboard or the next prompt. Forward the terminal size
  too (`bin/ai --width`/`--height`, from `TIOCGWINSZ`): the child cannot measure
  a terminal it is not attached to.
- **Help:** call `render_sink_help(out)` for the shared `Options:` block; the
  prompt's own help follows it. Completion derives every flag from that block.
- **Failure:** throw `prompt_error` like any prompt. The dispatcher catches it,
  applies the sink's `--on-error` policy, and keeps a failed source batch tainted
  so each later sink can decide again. A sink that is not run because the source
  batch failed is named on stderr (`prompt: .save not run: an earlier prompt
  failed; pass --on-error continue …`), so a skipped sink is never silent.
  Fan-out sinks are captured and emitted in argv order (no interleaved live
  streams); chained sinks stream as they run.
- **Passthrough, clearing and save hints:** a successful sink that emits no
  stdout is a no-op for the chain: it leaves the accumulated context and any
  failure taint unchanged. Register such a silent sink with `stream_output=false`
  so the dispatcher does not open an output separator for it; piped stdin is
  kept in the accumulated context instead of being spent by that sink. A sink
  that must reset the context sets the output flag `r.clear` (like
  `r.no_clipboard`): once that sink succeeds, the dispatcher drops the
  accumulated text, the filename hint, the redaction flag, the barrier stderr
  and the source-batch taint, while emitted output, the clipboard and the exit
  status stay untouched. `.clear` (`src/prompts/clear.cpp`) does only that;
  `.flush` (`src/prompts/save.cpp`) does it after a completed write, so a failed
  save keeps the context. A prompt that knows a good filename publishes the
  basename stem as the `keys::save_stem` fact (`r.published.set(keys::save_stem,
  stem)`), which the dispatcher carries through parallel children to later
  sinks; a prompt with no preferred name simply publishes nothing. `.save`
  (`src/prompts/save.cpp`) is the reference passthrough sink: it writes the same
  context an AI sink receives, refuses to overwrite unless `--force`, sanitizes
  the hint and appends `.md` to derived names. With no explicit `FILE`/`DIR` its
  `declare_` hook additionally publishes `keys::save_wants_name`, which is what
  makes a preceding `.delegate` ask the AI for a `filename:` (the plan's
  `save.stem=field:filename` binding then names the file); an explicit target
  suppresses the request. `tse`, `tse.codal`,
  `tse.intraday` and `files` provide hints. `.flush` shares `.save`'s parser,
  preflight and writer, with its own command name in messages. `.c.c`
  (`src/prompts/clipboard.cpp`) is the clipboard passthrough: it copies the
  context to the system clipboard and leaves it unchanged, so it can sit before
  another sink. Its paste counterparts `.clipboard` and `.c.p` are ordinary
  source prompts — a source never receives the chain context, which is exactly
  why copying has to be a sink.
- `ai.*` (`src/prompts/ai.cpp`) is the reference family: five web provider
  registrations plus the `ai.debate` relay, one shared executor,
  `--print-prompt` passed through to `bin/ai`, and `--stdin-only` so positional
  prompt text cannot silently replace the chain. Two more registrations
  (`.ai.lms`, `.ai.openai`) reuse that executor for the OpenAI-compatible HTTP
  providers; the `ai_provider` struct carries an `api` flag (and `local` for the
  local server), which switches the help's provider-options block — endpoint
  (`--base-url`/`--api-key`/`--param`) and inline attachments instead of a
  browser session and `--prompt-file` — while the sink plumbing is untouched.
  `.ai.debate` is a thin wrapper
  over `bin/ai.debate` (the providers passed as leading arguments, API ones
  included); the
  relay loop, framing and context-file writing live in `bin/ai/debate.py`,
  which reuses `bin/ai/cli.py`'s turn/renderer/context helpers. `-c`/`--new`
  are forwarded too; the panel state is stored by `bin/ai/state.py` under
  `$XDG_STATE_HOME/ai/debate-<providers>.json`. Its completion
  callback (`complete_ai_debate`) offers the provider names (aliases included),
  and `PROVIDER=` after `-m`/`--model`.

Tests: `tests/test_sink.cpp` (flag parsing, streaming/tee, timeout), the
`plan_chain`/`ai.*`/`save` registration and `tse` declaration cases in
`tests/test_chaining.cpp`, `tests/test_facts.cpp` (the fact store and its two
merge rules, the wire codec, `segment_outcome` round-trip), `tests/test_clipboard.cpp`
(the pasted-text → instrument policy), `tests/test_clipboard_prompt.sh`
(end-to-end `.clipboard`/`.c.p` paste and `.c.c` copy through a stubbed xclip),
`tests/test_ai_sink.sh` (end-to-end `--tty`/`--width`/`--height` and
`--context-file` plumbing through stub providers), `tests/test_ai_debate_sink.sh`
(the same plumbing for `.ai.debate`, plus provider/flag forwarding),
`tests/test_save.sh` (end-to-end save targets, passthrough, `--head`,
no-clobber and sanitized names), and `tests/test_clear.sh` (context reset between
sections, piped stdin, hint and failure-taint reset, `.flush` == `.save` +
`.clear`, preflight); `tests/test_ai_debate.py` exercises the relay
loop offline (turn order, framing, `--rounds`/`--until-agree`, context
selection, failure), `tests/test_ai_lms.py` drives the API providers against a
threaded `http.server` stub (SSE mapping, discovery + caching, model defaulting,
`--param` forwarding, inline attachments, `-c` transcript replay, the
`--prompt-file` refusal and the error remedies), `tests/test_ai_gap.py` checks
the GapGPT turn-recovery path offline (the poll fallback carries the
`accessToken` bearer and reads the turn, a transport error or a `StreamError`
is non-fatal, and `_read_turn` survives a raising poll), and the
`ai_help`/`ai_lms_help`/`ai_openai_help`/`ai_print_prompt`/`ai_debate_help`/
`ai_debate_print_prompt` ctest smokes exercise an offline sink chain.

## Agentic chain layer (`.expect`, `.extract`, `.exec`, `.each`)

Four prompts turn a chain into an extract/act loop; `bin/md.extract` owns the
selector grammar and the response contracts, so the native prompts only shell
out to it.

- **`.expect [NAME=]SELECTOR…`** asks `md.extract --contract` for every entry,
  appends the combined instruction to the context (the next AI sink sees it) and
  publishes `keys::expect` as a **plan**: one newline-separated `[NAME=]SELECTOR`
  entry per argument, exactly one of them unnamed (the primary). A single
  unnamed selector is the degenerate plan.
- **`.extract [SELECTOR]`** is a sink that replaces the context with the
  selection (`md.extract --select`). No selector means the `keys::expect` plan,
  then `auto`. Each **named** plan entry runs `md.extract --select --one` and
  publishes its value as that fact (so `save.stem=field:filename` names the next
  `.save`), then the primary replaces the context. `--list` leaves the context
  alone and prints candidates on stderr; `--json` and `--pick` pass through.
- **`.exec [SELECTOR] [--] COMMAND…`** is a sink that runs one command and
  replaces the context with its output. With no command it extracts one from
  the context (selector → the plan's primary → `auto`); a context that is
  exactly one `prompt …` line is run directly, which is what makes
  `… .extract .exec` work. A child `prompt` chain runs with the normal chain
  semantics.
- **`.each [SELECTOR] [loop flags] BODY .end`** is a sink that loops `BODY` over
  `md.extract --items` items, one child `prompt` per item, in parallel. A
  selector omitted here means the plan's primary (`agent::primary_selector`).

### Bodies and the parser

`.each` registers with `takes_body=true`
(`register_sink(..., takes_body=true)`), so `parse_chain` keeps the raw body
tokens as `prompt_invocation::body` (a `string_view` span in `view::body`) and
the dispatcher's flag/help parser never sees them. The body starts at the first
dot token; everything before it is `.each`'s own flags and selector. Bodies
nest by counting `.each` openers, and a missing or unexpected `.end` is a parse
error (exit 2). The body is a normal chain, never a quoted string; item data is
substituted into its tokens before the child runs.

`.exec -- COMMAND…` is the other parser-level rule: `--` gives the sink the
rest of argv so a child `prompt` chain (`.exec -- prompt .tse …`) is not split
into segments of the outer chain. A `prompt` command without `--` is
diagnosed. Direct program arguments are used as argv verbatim; one token that
contains whitespace is treated as a command line and split with quotes and
backslashes only. Compound shell syntax (`| & ; < > \`` and `$(`) is refused.

### Contracts and item substitution

The shared grammar is `kind[:qualifier][@N]` — `table[:col1,col2]`,
`code[:lang]`, `section[:heading]`, `command[:program]`, `field[:label]`
(a `label: value` line), `frontmatter`, `any`, `auto`, `none`, or a bare
heading. `@N` selects the Nth match. `.delegate` publishes `command:prompt`
itself (plus a `save.stem=field:filename` binding when `.save` has no explicit
target), so its output feeds `.extract`/`.exec` without an explicit `.expect`.

`.each` substitutes `{field}` (any table column or item key), `{item}`,
`{body}`, `{index}`, `{n}` and `{file}` (a temporary file holding the item, for
`.files`). Item data only ever reaches argv: a whole-token placeholder whose
value resolves to a prompt name or `.end` is rejected. A `{field}` program is
allowed (it is dynamic, so the child's policy approves the resolved command).

### Execution and permissions

Each `.each` item is a child `prompt` process with `PROMPT_INNER=1`,
`PROMPT_INNER_DEPTH` (parent + 1; loops are capped at depth 2) and
`PROMPT_INNER_ALLOW` (the static-program grants). The parent owns the clipboard
and prints one redaction legend, so an inner process does neither.

`policy::engine` (`sdk/policy.*`) decides commands: built-ins (`tse*` and
`md.extract` allow; `sudo`, `su`, `dd`, `mkfs*`, `shutdown`, `reboot`,
`poweroff`, `halt` deny; everything else ask), then
`$XDG_CONFIG_HOME/prompt/policy` (or `~/.config/prompt/policy`), then
`<git-root>/.prompt/policy`, then `PROMPT_POLICY_ALLOW`/`PROMPT_POLICY_DENY`
and `PROMPT_INNER_ALLOW`/`PROMPT_INNER_YES`. Rules are
`allow|ask|deny PROGRAM [arg-globs…]`; argument patterns are a prefix (`**`
for the rest). More specific rules win, then the higher source; deny wins a full
tie, then allow, then ask. `--yes` turns an ask into an allow but never
overrides a deny. An ask with no `/dev/tty` is a soft failure with the rule
hint. Prompts are trusted code, but a generated `prompt …` chain is displayed
whole and approved through `require_chain`. `.each` pre-approves the static
`.exec` programs in its body once and passes the program names as
`PROMPT_INNER_ALLOW`; a dynamic command asks in the child.

`.each` flags: `-j/--jobs N` (default min(items, 8), capped at 64), `--limit N`,
`--stdin`, `--print`, `--list`, `--json`, `--no-header`, `--keep-files`,
`--on-item-error stop|skip|keep`, `--timeout SEC` (600), `--yes`. `--print`
prints the substituted body commands without running them.

Tests: the `agent_*` ctests (`expect` and its `plan`, `extract`, its `field`
selector and the `test_extract_bind.sh` binding/save round-trip, `exec`
print/allowed/denied/`--` chain/verbatim args, `each` print/run/list) plus the
body and `--` cases in `tests/test_chaining.cpp` and the offline
`md_extract_unit` (`tests/test_md_extract.py`: the `field` selector and the
combined contract).

## History store (`sdk/history.{hpp,cpp}`, `src/prompts/history*.cpp`)

The dispatcher records **every invocation** under
`${PROMPT_HISTORY_DIR:-${XDG_STATE_HOME:-~/.local/state}/prompt/runs/}`, so an
answer (e.g. from `prompt .verify .ai.dseek`) can be retrieved later. `main.cpp`
owns the lifecycle: it builds a `history::recorder` (with the effective argv,
cwd, git root, tty flag and the terminal identity from `terminal::current()`),
sets `PROMPT_HISTORY_CURRENT` to the run id, calls
`record_segment(...)` for each segment (in the no-sink accumulator loop and in
`run_normal_batch`, and for skipped sinks and `update_from_sink` merges) and
finishes with `history.finish(exit_code, incomplete)`. Recording is best-effort:
a write failure is swallowed and can never fail a prompt run.

- **One file per run**, append-only Markdown named
  `<YYYYMMDD-HHMMSS-t<mm>-<xxxx>>.md` (the id is sortable to the millisecond and
  deliberately never matches the sanitizer's phone-number pattern). The body is
  framed by `<!-- history:run {json} -->`, one `<!-- history:segment {json} -->`
  per segment and a closing `<!-- history:end {json} -->` (the run marker also
  stores the terminal identity); each marker carries an
  exact `bytes` (and `input_bytes`) count, so a segment body that contains a
  look-alike marker can never corrupt the parse (a truncated file loads
  partially and is marked `incomplete`).
- **Deltas, not the whole context.** A segment stores its own output
  (`text`), not the growing chain; `merge` records how it folded
  (`append`/`replace`/`skip`/`clear`/`abort`/`failed`). A sink additionally stores
  `input` only when the context it received differs from the one replayed from
  the recorded merges — `history::barrier_input(run, index)` returns the stored
  input when present, otherwise replays the merges exactly as
  `chain_state::append` does.
- **Replay.** `history::final_output(run)` reproduces what the dispatcher printed:
  the emitted segments joined by the one text-fold rule (`text::append_block`, the
  same rule `chain_state::append`, `output_accumulator` and the clipboard use) —
  trim both sides, one blank line between blocks, one trailing newline — with the
  redaction legend first when any emitted segment was redacted. `.history` prints
  exactly that, so `prompt .history` byte-matches the original stdout.
- **API.** `run_files`/`run_ids` (newest first), `load`/`load_all`,
  `find_by_id` (an id or prefix) and `find_by_rank` (1 = newest), the
  terminal-scoped `in_terminal`/`find_last_in`/`find_rank_in` (what `.last` and
  `--here` use; only the run marker is read while scanning), `final_output`,
  `barrier_input`, `parse_duration`/
  `parse_size`/`human_bytes`, `keep_runs`/`keep_bytes`/`auto_prune`, `prune`
  (oldest first, never the run passed as protected), and `display_path` (a path
  relative to the cwd when possible, so `$HOME` redaction does not corrupt a
  printed path).
- **Prompts.** `src/prompts/history.cpp` registers `.history` (default emitted
  text; `--segment`, `--input`, `--all`, `--meta`, `--head`, `--here`; publishes
  `keys::save_stem` = the run id), `.last`/`.history.last` (the terminal-scoped
  newest run, `--any` for the global view; the shared `resolve_run` reads a
  digits-only key as a rank when `1..count` and otherwise as an id prefix),
  `.history.list` (`--limit`, `--since`,
  `--paths`, `--json`, `--head`, `--here`) and `.history.dir`;
  `src/prompts/history_find.cpp` registers `.history.find` (QUERY, `--regex`,
  `--case-sensitive`, `--name`, `--since`, `--limit`, `--context`, `--paths`,
  `--json`, `--here`; exit 1 via `soft_error` on no match);
  `src/prompts/history_purge.cpp` registers `.history.purge` (`--older-than`,
  `--keep`, `--max-size`, `--all`, `--dry-run`; protects `--keep` newest runs and
  `PROMPT_HISTORY_CURRENT`; `no_clipboard`). All seven register with
  `record = false`.
- **Child runs.** An `.each` item (and a `.exec` child `prompt` chain) records its
  own run; `each.cpp`/`exec.cpp` pass `PROMPT_HISTORY_PARENT=<current run id>` so
  the child is tagged `parent`. `PROMPT_HISTORY_INNER=0` silences children.
- **Knobs.** `PROMPT_HISTORY` (0/off disables recording), `PROMPT_HISTORY_DIR`,
  `PROMPT_HISTORY_MAX_BYTES` (4 MiB per file, over-long writes are truncated),
  `PROMPT_HISTORY_KEEP` (1000), `PROMPT_HISTORY_SIZE` (256M),
  `PROMPT_HISTORY_INNER`, `PROMPT_HISTORY_PRUNE` (0 = no automatic pass),
  `PROMPT_HISTORY_CURRENT`, plus the terminal identity overrides
  `PROMPT_TERMINAL_SID`/`PROMPT_TERMINAL_TTY`/`PROMPT_TERMINAL_KEY` (see
  `terminal.hpp` below).
- **Tests.** `tests/test_history.cpp` (marker framing, a fake marker inside a
  body, truncation, replay, ordering/ranks, the terminal identity and scoping,
  prune) and
  `tests/test_history.sh` (end-to-end: `.history` == stdout for plain and sink
  chains, list/find/purge, `.last`/`--here`/`--any` and the fallback note, the
  env knobs, inner-run tagging, completion). The
  tests `ENVIRONMENT` every ctest in `tests/CMakeLists.txt` onto a scratch
  `PROMPT_HISTORY_DIR` so the suite never touches the real store.

## SDK helpers (`src/sdk/`)

Use these instead of re-open-coding the same loops:

- `args.hpp`:
  - `parse_head_option(args, head_lines, strict)` — the raw `--head` validator.
  - `head_lines_or_throw(v, strict = true)` — the parser plus the
    `throw usage_error{...}` preamble, for prompts that only cap output.
  - `file_options_or_throw(v, strict = true)` — the file prompts' whole CLI:
    the validated `--head N` cap plus the value-less `--num`/`--numbered` flag,
    returned as a `file_options {head_lines, numbered}` (built on
    `is_number_flag(arg)`, whose spellings live in `kNumberFlags`).
  - `positional_args(v)` / `collect_file_args(v)` — the FILE arguments with
    the `--head <value>` pair and the `--num`/`--numbered` flags removed.
    `collect_file_args` splits them into `files` (regular files) and `missing`;
    embed the first list and warn about the second. A sibling `.files` embeds
    its own list, so a file prompt never re-embeds a sibling's files.
  - `file_usage_help(name, tail, pipe, prefix)` and
    `file_options_help(what, show_help)` — the shared usage lines and the
    `--head`/`--num` Options block, so every file prompt's help stays identical.
- `embed.hpp`: `infer_lang` (`std::string_view`), `trim_context`,
  `trim_context_nl`, `head_output`, `read_file`, `number_lines(text, numbered)`
  (the `N | line` prefixing `--num` enables), `embed_file`,
  `embed_file_block(file, file_options)`, `warn_missing_file`, `embed_stdin`.
- `stage.hpp`: `environment`, `stream_config`, `stdin_source`, `view`, `result`,
  `declaration`, `run_sub_prompt(parent, name, args, stdin_text)` (runs another
  registered prompt in-process with a fresh result — how `auto`, `verify`, `run`
  and `paths` compose other prompts) and the free services taking an
  `environment`: `clipboard_content(env)`, `clipboard_identifier(env)`,
  `select_files(env)`, `resolve_input_file(env, wanted)`. `environment` is a
  plain data bag built once at startup (`src/environment.cpp`'s
  `make_environment()`, not `main.cpp`); `view` holds references to it, to the
  prior facts and to the stdin source, so it is aggregate-initialised and
  non-assignable (return it by value only when the referents outlive it). The
  "what does this pasted text identify" policy (easytrader/TSETMC/Codal URLs,
  ISIN, insCode, a short Persian symbol) lives beside the clipboard it reads:
  `core/clipboard.hpp`'s `clipboard::detect_identifier()`.
- `facts.hpp`: the fact store and `keys::`; `wire.hpp`: `segment_outcome`,
  `encode_outcome`/`decode_outcome`, `drain_all`; `chain_state.hpp`: the
  dispatcher's accumulated text plus known facts and the redaction/incomplete
  flags.
- `terminal.hpp`: `terminal::id {sid, tty, key}`, `current()` (the
  `PROMPT_TERMINAL_SID`/`PROMPT_TERMINAL_TTY`/`PROMPT_TERMINAL_KEY` overrides
  first, then `getsid(0)` and the controlling tty), `same()` (the identity
  comparison: an opaque key wins when both sides carry one, otherwise the
  session ids must agree and a differing tty rejects a recycled pid) and
  `describe()`. The history store is its first user (`.last`, `--here`), but it
  is deliberately history-agnostic so any future per-terminal feature can share
  the same "a place, not a global" identity.
- `agent.hpp`: the agentic layer's shared pieces — `is_selector_token`,
  `split_command` (quotes/backslashes, compound-syntax refusal), `join_argv`,
  `parse_exec_args` (`.exec`'s own flags plus command/selector split),
  `program_name`, `static_exec_commands` (the `.exec` programs a `.each` body
  will run, nested bodies included) and `validate_exec_template` (a
  malformed `.exec`, before any child runs).
- `policy.hpp`: the permission engine — `engine::require(argv)`,
  `require_chain(tokens)`, `set_yes`, the rule sources and the `/dev/tty` ask.
- `sink.hpp` / `sink_types.hpp`: `register_sink` support — `parse_sink_args`,
  `render_sink_help`, `trim_context_lines`, `write_stdout`, `write_stderr` and
  `run_sink_tool` (see "Sinks").
- `src/core/process.hpp`: `process::command` / `run_command(argv, stdin, env,
  timeout)` — fork/exec, no shell, no quoting. `on_stdout`/`on_stderr` tee each
  chunk while still accumulating it, and the poll loop enforces the timeout
  while reading. `run_command` defaults to 30 s, so a long fetch passes its own
  timeout (`tse_data::run_tool` allows 10 min and turns a timeout, non-zero exit
  or empty dataset into a `prompt <name>: bin/<tool> …` message).
  `src/core/fs.hpp::bin_tool` locates sibling `bin/` tools (exe dir → git root →
  cwd → `$PATH`).

## Shell completion (`--complete`, `--complete-line`)

Every candidate a shell offers comes from the binary: `../shell/completions.fish`
forwards the command line and prints what it gets back, so the completion logic
lives next to the chain semantics it mirrors instead of in the shell script.
Both entry points print `value<TAB>description` lines and both are answered
early in `main.cpp` (before the clipboard is initialized and before stdin is
read), so completion never runs a prompt, consumes the pipe or copies anything.

- `prompt <name> --complete <prefix> [args typed before it...]` — one prompt's
  own candidates (alias `prompt --complete <name> [prefix] ...`).
- `prompt --complete-line <current> [tokens after `prompt`]` — a whole command
  line (`sdk/line_completion.cpp`), which fish calls:
  `prompt --complete-line "$current" $tokens[2..-1]`, under two conditions only
  (name position/step boundary with `-f` so no file noise, every argument
  position without it so fish's own path completion still applies).

- **Flags are automatic.** `completion::write_help_flags` parses the `Options:`
  block of the prompt's own `render_help_fn` output, so every prompt completes
  `--head`, `--num`, `--days`, `--format`, … with no per-file code; both
  spellings of `--type, -t` are emitted. `write_prompt_candidates` (the shared
  `sdk/completion.cpp` renderer) is what both forms call, so a prompt answers
  identically either way.
- **Positional/value candidates** come from the prompt's optional
  `completion_fn` — the fifth, defaulted `register_prompt` argument
  (`sdk/completion.hpp`, embedded by `new`). Put the callback next to the
  prompt's argument parser and use the helpers: `completion::matches`,
  `write_item`, `write_items`, `write_values`, and `write_values_after_flag`
  (completes a flag's value when it is the previous argument).
  `tse_data::complete_instrument` delegates the `tse`/`tse.intraday`
  identifier lookup to `bin/tse.resolve --complete`; `skill`, `run`, `diagram`,
  `metadata`, `opportunities` and `overengineering` have small callbacks.
- **The line mode** replays just enough of `parse_chain` to know what the
  current token is being completed against: a bare token opens a step only while
  none matched (the head), a `.name`/`-name` token naming a prompt **or a
  function** opens the next one, later tokens are that step's arguments
  (`write_step_names` covers the head and the boundaries with the catalog plus
  the visible functions, in all three spellings, merged into one locale-aware
  order). A `--` tail and an open `.each` body are opaque and produce nothing.
  A half-typed chain is normal input, so the scan never calls `parse_chain`.
- **Functions** are completed from the same place the expander resolves them:
  the line is first run through the `*.prompt` pass
  (`expand_prompt_file_tokens`, so a definition living in an inlined file is
  found), then the global `funcs` files and the inline `.func … .end` blocks
  (`collect_inline_functions`) supply the names. The `.func` keyword is offered
  at the top level, `.end` inside a `.func` body, and the body itself completes
  as a chain. `.func` is not offered inside a body and the signature token is
  left alone.
- **`*.prompt` candidates** live with the resolver they mirror,
  `write_prompt_file_candidates` in `sdk/prompt_file.cpp`: the same three
  sources, nearest first, each candidate naming its source (`prompt file`,
  `prompt file (project)`, `prompt file (config)`) and a shared name emitted
  once; a token containing `/` lists that directory instead, offering its
  `*.prompt` files and its subdirectories so the user can descend. They are
  offered at the head, and at an argument position only for a deliberate
  `.prompt`/path token (a plain word stays a word, which is what keeps
  `.files src/ma<TAB>` on fish's path completion). `--no-prompt-files` anywhere
  in the tokens suppresses them.
- `prompt_descriptor::complete_fn` stays `nullptr` when a prompt registers no
  callback (flags only). Tests: `tests/test_completion.cpp` plus the
  `complete_flags`/`complete_values`/`complete_line_*` ctest smokes.

## Output bytes

The dispatcher's output is the contract the tests and the AI-facing prose rely
on; there is no other implementation to stay compatible with. Three behaviours
are deliberate and worth keeping in mind:

- **One text-fold rule.** Every emitted segment goes through
  `text::append_block` (`src/core/strings.{hpp,cpp}`): the block is trimmed of
  leading and trailing newlines, an empty segment contributes nothing (no stray
  separator), consecutive blocks are separated by exactly one blank line, and the
  buffer ends with exactly one newline. The chain context (`chain_state::append`),
  the stdout accumulator, the clipboard builder (`output_emitter`) and the history
  replay (`history::final_output`) all share it, so stdout, the clipboard and the
  context a sink receives have the same shape. Prompts therefore never trim their
  own output for presentation — except `.c.p`, which normalizes before its own
  `--head N` so the cap counts real content lines.
- The dispatcher prepends a one-line **redaction legend**
  (`sanitize::kRedactionLegend`) whenever `clean_privacy` rewrites personal data
  (`HOME`, `USER`, `HOSTNAME`, `IP_ADDRESS`, `MAC_ADDRESS`, `EMAIL_ADDRESS`,
  `PHONE_NUMBER`, `SSN`). It is a one-line header, so it sits directly above the
  first redacted block with no blank line between them.
- Prompts such as `tse`, `tse.rigor` and `tse.times` and the `--num`/`--numbered`
  options are specific to this dispatcher; the shared help helpers standardise
  the usage lines across prompts.

When changing output, prefer the shared helpers (`file_usage_help`,
`file_options_help`) and keep the wording consistent across prompts.

## Rendering text

- Prefer a raw string literal (`R"PROMPT(...)PROMPT"`) for multi-line prose and
  `std::format` for interpolation.
- The usage lines and the shared `--head`/`--num` options come from
  `file_usage_help` / `file_options_help` (`sdk/args.hpp`); don't re-type them.
- `new` (`new.cpp`) generates a new native prompt and embeds the SDK headers
  plus `fix.cpp` as assets. Those snapshots are baked in at build time from
  `PROMPT_ASSETS` in `CMakeLists.txt` (add a header there when the guidance
  should include it); keep `new.cpp`'s guidance text in sync with the helpers.
