# AGENTS.md — `prompts/` (native prompt dispatcher, C++)

Native C++ replacement for the bash dispatcher under `../prompts-shell/`. This
directory builds the `prompt` command; the bash side is a frozen reference for
the parity harness only. The repo-wide guide is `../AGENTS.md`; this file is the
working guide for this subtree.

## Build, test, lint

```bash
cmake -S prompts -B prompts/build && cmake --build prompts/build -j 8      # Release
cmake --build prompts/build-asan -j 8 && (cd prompts/build-asan && ctest)  # Debug + tests
clang-format -i prompts/src/**/*.{cpp,hpp} && clang-format --dry-run -Werror prompts/src/**/*.{cpp,hpp}
clang-tidy -p prompts/build src/*.cpp src/*/*.cpp tests/*.cpp              # zero findings
cmake --install prompts/build --prefix "$HOME/cmd" --component prompt      # -> bin/prompt
```

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
- `<summary>` is the first help paragraph verbatim; it is what `prompt list`
  prints. The help's first line must be `Usage: prompt <name>`.
- The name is passed explicitly because dotted/dashed names (`git.diff`,
  `tse.find`, `clang-tidy`) cannot be derived from the identifier.

## Prompt contract

- Write results onto the context: `ctx.out` (stdout — sanitised, piped onward,
  clipboarded), `ctx.err` (stderr, exit stays 0), `ctx.no_clipboard`.
- Fail by **throwing** (`src/sdk/errors.hpp`): `usage_error` (exit 2),
  `soft_error` (exit 1), or `prompt_error` with `message`, `partial_output`,
  `no_clipboard`, `stdin_consumed`. `main.cpp` catches them once and reproduces
  the bash bytes; a failed prompt's partial stdout is still accumulated and the
  chain continues.
- Chained prompts share the **original** piped stdin. A prompt that reads stdin
  (`read_stdin` / `embed_stdin`) spends the pipe for later prompts by setting
  `ctx.stdin_consumed`.

## SDK helpers (`src/sdk/`)

Use these instead of re-open-coding the same loops:

- `args.hpp`:
  - `parse_head_option(args, head_lines, strict)` — the raw `--head` validator.
  - `head_lines_or_throw(ctx, strict = true)` — the parser plus the
    `throw usage_error{...}` preamble most prompts open with.
  - `positional_args(ctx)` — the arguments with the `--head <value>` pair
    removed (`std::vector<std::string>`).
  - `collect_file_args(ctx)` — splits FILE arguments into `files` (regular
    files) and `missing`; embed the first list and warn about the second.
- `embed.hpp`: `infer_lang` (`std::string_view`), `trim_context`,
  `trim_context_nl`, `head_output`, `read_file`, `embed_file`,
  `embed_file_block`, `warn_missing_file`, `embed_stdin`.
- `prompt_context.hpp`: the struct is a plain data bag; the dispatcher-injected
  services are free functions taking the context — `clear_output(ctx)`,
  `clipboard_content(ctx)`, `clipboard_identifier(ctx)`, `select_files(ctx)`,
  `resolve_input_file(ctx, wanted)`.
- `src/core/process.hpp`: `process::command` / `run_command(argv, stdin, env,
  timeout)` — fork/exec, no shell, no quoting. `src/core/fs.hpp::bin_tool`
  locates sibling `bin/` tools (exe dir → git root → cwd → `$PATH`).

## Output bytes and parity

Prompt stdout/stderr/help are byte-locked to `../prompts-shell/`. The harness
(`~/parity/`: `gen_cases.py` + `parity.sh` + `run_all.sh`) runs both dispatchers
and diffs live output — there are no golden files, so an output-neutral
refactor is verified by re-running it.

```bash
env -u WAYLAND_DISPLAY -u KDE_SESSION_VERSION -u DISPLAY PATH=~/parity/fakebin:$PATH \
    bash ~/parity/gen_cases.py && bash ~/parity/run_all.sh
```

- `#native-only <names>` arms a one-shot filter that drops those whole lines
  from the native side of the next case (used by `tse`/`rigor`, which have no
  bash counterpart).
- The native dispatcher prepends a one-line **redaction legend**
  (`sanitize::kRedactionLegend`) whenever `clean_privacy` rewrites personal data
  (`HOME`, `USER`, `HOSTNAME`, `IP_ADDRESS`, `MAC_ADDRESS`, `EMAIL_ADDRESS`,
  `PHONE_NUMBER`, `SSN`). This is a deliberate divergence from the bash
  reference; `parity.sh` strips that line from the native output.

## Rendering text

- Prefer a raw string literal (`R"PROMPT(...)PROMPT"`) for multi-line prose and
  `std::format` for interpolation; keep the exact bytes the bash script emits.
- `new` (`new.cpp`) generates a new native prompt and embeds the SDK headers
  plus `fix.cpp` as assets. Those snapshots are baked in at build time from
  `PROMPT_ASSETS` in `CMakeLists.txt` (add a header there when the guidance
  should include it); keep `new.cpp`'s guidance text in sync with the helpers.
