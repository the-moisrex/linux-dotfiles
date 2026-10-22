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
  `static void execute_<name>(prompt_context&)` and
  `static void render_help_<name>(std::ostream&)`, ending with
  `bool const registered = prompt::register_prompt("<name>", "<summary>", &execute_<name>, &render_help_<name>);`.
  CMake globs `src/prompts/*.cpp`, so no other file changes; registration runs at
  static-init time, which is why `prompt_lib` must stay an OBJECT library.
  A data prompt and its data-free lens may share one file when they belong
  together (`tse_intraday.cpp` registers `tse.intraday` and
  `tse.intraday.analysis`); give each its own `bool const` registration
  variable.
- `<summary>` is the first help paragraph verbatim; it is what `prompt list`
  prints. The help's first line must be `Usage: prompt <name>`.
- The name is passed explicitly because dotted/dashed names (`git.diff`,
  `tse.find`, `clang-tidy`) cannot be derived from the identifier.
- A **sink** uses `prompt::register_sink(...)` instead (see "Sinks" below). It
  needs only its executor, help renderer and `sink_policy`; the dispatcher owns
  the shared flags and the barrier ordering.

## Prompt contract

- Write results onto the context: `ctx.out` (stdout — sanitised, piped onward,
  clipboarded), `ctx.err` (stderr, exit stays 0), `ctx.no_clipboard`.
- Fail by **throwing** (`src/sdk/errors.hpp`): `usage_error` (exit 2),
  `soft_error` (exit 1), or `prompt_error` with `message`, `partial_output`,
  `no_clipboard`, `stdin_consumed`. `main.cpp` catches them once; a failed
  prompt's partial stdout is still accumulated and the chain continues.
- Chained prompts share the **original** piped stdin. A prompt that reads stdin
  (`read_stdin` / `embed_stdin`) spends the pipe for later prompts by setting
  `ctx.stdin_consumed`. With no piped stdin the dispatcher runs independent
  segments in parallel (forked children; prompts must not assume they are the
  only segment running) and emits their results in argv order. `ctx.chain`
  carries every invocation (`name`, `args`) plus `ctx.chain_index`, which is how
  the `tse` prompts borrow a sibling's symbol.

## Sinks

A sink is a chain barrier, not an ordinary prompt. Register it with
`prompt::register_sink("<name>", "<summary>", &execute_<name>, &render_help_<name>, nullptr, policy, stream_output)`
(`sink_policy::chain` by default; `fanout`/`exclusive` are descriptor-level
choices; `stream_output=false` marks a silent sink). Adding a sink does not
touch the AI code or the dispatcher: the descriptor role makes `main.cpp` batch
everything before it, hand the sink the accumulated `ctx.chain_context`, and let
its answer become the context later segments see.

- **Shared flags are dispatcher-owned** (`sdk/sink_types.hpp` + `sink.hpp`):
  `--on-error abort|continue|skip` (default abort), `--with-context`,
  `--show-context` and `--head N`. `parse_sink_args` removes them once in
  `main.cpp`; the sink implementation only sees its own arguments.
- **Reading the barrier:** `wait_for_prompts(ctx)` returns
  `ctx.chain_context`. `ctx.chain_stderr` carries the barrier's stderr and is
  deliberately not part of the AI context. `ctx.sink` holds the parsed flags
  (`head_lines` has already been applied to `ctx.chain_context` by the
  dispatcher — do not trim it again; `trim_context_lines` is the shared helper
  behind it, applied before the redaction legend is prepended).
- **Streaming:** use `write_stdout(ctx, chunk)` and `write_stderr(ctx, chunk)`.
  They append to `ctx.out`/`ctx.stderr_buffer` and, when the dispatcher enabled
  streaming, call `ctx.stdout_writer`/`ctx.stderr_writer` immediately.
  `run_sink_tool(ctx, argv, stdin, timeout)` wires both to
  `process::command`'s `on_stdout`/`on_stderr` tee callbacks.
- **Terminal rendering:** a tool-backed sink's stdout is a pipe, so a tool that
  checks `isatty()` would drop its colour/Markdown. Pass its force-terminal flag
  (as `ai.*` does with `bin/ai --tty`) when `isatty(STDOUT_FILENO)`, keep the
  styled bytes display-only, and take the chain context from a plain side
  channel — `ai.*` reads `bin/ai --context-file` back into `ctx.out`, so rich
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
  that must reset the context sets the output flag `ctx.clear_context` (like
  `ctx.no_clipboard`): once that sink succeeds, the dispatcher drops the
  accumulated text, the filename hint, the redaction flag, the barrier stderr
  and the source-batch taint, while emitted output, the clipboard and the exit
  status stay untouched. `.clear` (`src/prompts/clear.cpp`) does only that;
  `.flush` (`src/prompts/save.cpp`) does it after a completed write, so a failed
  save keeps the context. A prompt that knows a good filename sets
  `ctx.save_hint` (a basename stem); `main.cpp`
  carries the nearest successful hint through parallel children to later sinks
  as `ctx.chain_save_hint`. `.save` (`src/prompts/save.cpp`) is the reference
  passthrough sink: it writes the same context an AI sink receives, refuses to
  overwrite unless `--force`, sanitizes the hint and appends `.md` to derived
  names. `tse`, `tse.codal`, `tse.intraday` and `files` provide hints; a
  prompt with no preferred name simply leaves `save_hint` empty. `.flush`
  shares `.save`'s parser, preflight and writer, with its own command name in
  messages.
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
`plan_chain`/`ai.*`/`save` registration cases in `tests/test_chaining.cpp`,
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
  - `head_lines_or_throw(ctx, strict = true)` — the parser plus the
    `throw usage_error{...}` preamble, for prompts that only cap output.
  - `file_options_or_throw(ctx, strict = true)` — the file prompts' whole CLI:
    the validated `--head N` cap plus the value-less `--num`/`--numbered` flag,
    returned as a `file_options {head_lines, numbered}` (built on
    `is_number_flag(arg)`, whose spellings live in `kNumberFlags`).
  - `positional_args(ctx)` / `collect_file_args(ctx)` — the FILE arguments with
    the `--head <value>` pair and the `--num`/`--numbered` flags removed.
    `collect_file_args` splits them into `files` (regular files) and `missing`;
    embed the first list and warn about the second.
  - `file_usage_help(name, tail, pipe, prefix)` and
    `file_options_help(what, show_help)` — the shared usage lines and the
    `--head`/`--num` Options block, so every file prompt's help stays identical.
- `embed.hpp`: `infer_lang` (`std::string_view`), `trim_context`,
  `trim_context_nl`, `head_output`, `read_file`, `number_lines(text, numbered)`
  (the `N | line` prefixing `--num` enables), `embed_file`,
  `embed_file_block(file, file_options)`, `warn_missing_file`, `embed_stdin`.
- `prompt_context.hpp`: the struct is a plain data bag; the dispatcher-injected
  services are free functions taking the context — `clear_output(ctx)`,
  `clipboard_content(ctx)`, `clipboard_identifier(ctx)`, `select_files(ctx)`,
  `resolve_input_file(ctx, wanted)`. Sink fields (`chain_context`,
  `chain_stderr`, `chain_save_hint`, `sink`, stream writers/flags) live here
  too; `save_hint` is a prompt's optional output-side filename stem, cleared by
  `clear_output`.
- `sink.hpp` / `sink_types.hpp`: `register_sink` support — `parse_sink_args`,
  `render_sink_help`, `wait_for_prompts`, `write_stdout`, `write_stderr` and
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
