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

## Prompt contract

Two types, one direction each: `prompt::view` is everything a segment may
**read**, `prompt::result` is everything it may **produce**. Nothing in a prompt
mutates chain state, which is what makes forked parallel segments safe.

- **Reading** (`sdk/stage.hpp`): `v.name`, `v.args` (a span — `.empty()`,
  `.size()`, indexed), `v.in.text()` (the shared stdin, a `string_view`),
  `v.env.git_root`/`v.env.exe_path`, `v.text` (the accumulated chain context,
  which a sink reads) and `v.prior` (the facts earlier segments declared or
  published). `v.stream` is the live-terminal writers, set by the dispatcher for
  a streaming sink only.
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
order resolve the same instrument. `.files` needs no fact for a sibling file
prompt — its output already carries the embedded files into the chain, and
re-embedding them would duplicate the content. Facts are deliberately opt-in:
add a key when two prompts genuinely cannot work it out from their own
arguments, not as a general channel.

The fork wire (`sdk/wire.hpp`) carries a `segment_outcome` — stdout, stderr, the
separate stderr buffer, the published facts, the output flags and the exit
status — as one length-prefixed blob per child; `drain_all()` polls every child
pipe at once instead of draining them in turn.

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
  the hint and appends `.md` to derived names. `tse`, `tse.codal`,
  `tse.intraday` and `files` provide hints. `.flush` shares `.save`'s parser,
  preflight and writer, with its own command name in messages.
- `ai.*` (`src/prompts/ai.cpp`) is the reference family: five provider
  registrations plus the `ai.debate` relay, one shared executor,
  `--print-prompt` passed through to `bin/ai`, and `--stdin-only` so positional
  prompt text cannot silently replace the chain. `.ai.debate` is a thin wrapper
  over `bin/ai.debate` (the five providers passed as leading arguments); the
  relay loop, framing and context-file writing live in `bin/ai/debate.py`,
  which reuses `bin/ai/cli.py`'s turn/renderer/context helpers. `-c`/`--new`
  are forwarded too; the panel state is stored by `bin/ai/state.py` under
  `$XDG_STATE_HOME/ai/debate-<providers>.json`. Its completion
  callback (`complete_ai_debate`) offers the provider names, and `PROVIDER=`
  after `-m`/`--model`.

Tests: `tests/test_sink.cpp` (flag parsing, streaming/tee, timeout), the
`plan_chain`/`ai.*`/`save` registration and `tse` declaration cases in
`tests/test_chaining.cpp`, `tests/test_facts.cpp` (the fact store and its two
merge rules, the wire codec, `segment_outcome` round-trip), `tests/test_clipboard.cpp`
(the pasted-text → instrument policy),
`tests/test_ai_sink.sh` (end-to-end `--tty`/`--width`/`--height` and
`--context-file` plumbing through stub providers), `tests/test_ai_debate_sink.sh`
(the same plumbing for `.ai.debate`, plus provider/flag forwarding),
`tests/test_save.sh` (end-to-end save targets, passthrough, `--head`,
no-clobber and sanitized names), and `tests/test_clear.sh` (context reset between
sections, piped stdin, hint and failure-taint reset, `.flush` == `.save` +
`.clear`, preflight); `tests/test_ai_debate.py` exercises the relay
loop offline (turn order, framing, `--rounds`/`--until-agree`, context
selection, failure), and the `ai_help`/`ai_print_prompt`/`ai_debate_help`/
`ai_debate_print_prompt` ctest smokes exercise an offline sink chain.

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

## Shell completion (`--complete`)

`prompt <name> --complete <prefix> [args typed before it...]` prints the
prompt's completion candidates — one `value<TAB>description` per line — and
exits 0 without running the prompt, reading stdin or touching the clipboard.
`main.cpp` handles it early (before clipboard init and stdin), and
`../shell/completions.fish` (`__fish_prompt_complete`) is the caller.

- **Flags are automatic.** `completion::write_help_flags` parses the `Options:`
  block of the prompt's own `render_help_fn` output, so every prompt completes
  `--head`, `--num`, `--days`, `--format`, … with no per-file code; both
  spellings of `--type, -t` are emitted.
- **Positional/value candidates** come from the prompt's optional
  `completion_fn` — the fifth, defaulted `register_prompt` argument
  (`sdk/completion.hpp`, embedded by `new`). Put the callback next to the
  prompt's argument parser and use the helpers: `completion::matches`,
  `write_item`, `write_items`, `write_values`, and `write_values_after_flag`
  (completes a flag's value when it is the previous argument).
  `tse_data::complete_instrument` delegates the `tse`/`tse.intraday`
  identifier lookup to `bin/tse.resolve --complete`; `skill`, `run`, `diagram`,
  `metadata`, `opportunities` and `overengineering` have small callbacks.
- `prompt_descriptor::complete_fn` stays `nullptr` when a prompt registers no
  callback (flags only). Tests: `tests/test_completion.cpp` plus the
  `complete_flags`/`complete_values` ctest smokes.

## Output bytes

The dispatcher's output is the contract the tests and the AI-facing prose rely
on; there is no other implementation to stay compatible with. Two behaviours are
deliberate and worth keeping in mind:

- The dispatcher prepends a one-line **redaction legend**
  (`sanitize::kRedactionLegend`) whenever `clean_privacy` rewrites personal data
  (`HOME`, `USER`, `HOSTNAME`, `IP_ADDRESS`, `MAC_ADDRESS`, `EMAIL_ADDRESS`,
  `PHONE_NUMBER`, `SSN`).
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
