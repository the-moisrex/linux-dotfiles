# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]


### Added

- `cpp_prompt/src/tse/tse_markdown.cpp` — C++ port of `bin/tse`'s `markdown()`: renders `.stock` snapshots byte-for-byte like a live fetch (same headings, JSON compact dumps, CSV tables) without an interpreter
- `process::command` — fluent subprocess builder (fork/exec, `stderr_mode` inherited/captured/`2>&1`, timeout, env) plus `run_script()` for `/bin/bash -c`; the dead `run_pipeline()`/`run_async()` wrappers are gone
- `prompt::text` string utilities (`core/strings.hpp`), `embed_file_block()`, `warn_missing_file()`, `help_text()` — the repeated file-embedding, warning and help-capture patterns every prompt used to open-code
- `cpp_prompt/tests/test_errors.cpp` — the `prompt_output` / `prompt_error` exception surface (exit codes, partial stdout, clipboard opt-out, stdin-spent flag)
- `cpp_prompt/` — native C++ prompt dispatcher with **all prompts implemented natively**: no runtime dependency on the `prompts/` directory (no scanning/reading/executing of prompt scripts; `new` embeds static snapshots instead). Every prompt's stdout, stderr, exit codes, `--help` text and `prompt list` summary stay byte-identical to `bin/prompt` + `prompts/*.sh`
- Native ports for the remaining prompt families: text/analysis (`api`, `explain`, `debug`, `diagram`, `metadata`, …), git (`git`, `git.diff`, `git-dirty`, `git.files`, `git.worktree.*`, `repo`, `verify`), tool wrappers (`clang-tidy`, `cmake`, `gdb`, `cppman`, `ci`, `docker`, `gh.issue`, `paths`, …), stdin/translate (`english`, `farsi`, `stdin`, `tweets`, `symbols`, `summarize`, `yt`), complex (`agents`, `ai-said`, `ask-for-help`, `skill`, `opportunities`, `fs8.mod`, `fs8.test`), plus the shared dispatch/chaining/pipeline/clipboard/sanitize SDK
- `prompt::fs::bin_tool` — locates repo `bin/` utilities (exe dir → git root → cwd walk → PATH) without going through the prompt scripts
- `prompt/prompts/prompt_assets.hpp` — static snapshots of `_common.sh`/`fix.sh`/`symbols.sh` embedded into the binary for the `new` prompt

### Changed

- `cpp_prompt/` structure: prompts now return `prompt_output` and **throw** on failure (`usage_error`/`soft_error`/`prompt_error` in `prompt/sdk/errors.hpp`); the dispatcher catches them once in `main.cpp`'s chain loop. `prompt_result` and the 76 per-prompt header pairs are gone — one umbrella header, `include/prompt/prompts/prompts.hpp`
- Subprocesses run through fork/exec instead of `popen` + shell-quoting: argv reaches the child verbatim, child stderr can be captured or merged, and timeouts are enforced
- Repeated `output += "literal" + x + "literal"` sites (143) converted to `std::format`; lying `noexcept` qualifiers removed (they aborted on `std::stoull`/`std::regex` failures) and the numeric parsing hardened
- `stock`/`intraday` fetch through `python3 bin/tse … --markdown` exactly like `stock.sh` (the tool's stderr is inherited); skill frontmatter parsing and the KDE clipboard path are C++/`qdbus` — `cpp_prompt` no longer embeds or links libpython (`src/core/python.cpp`, `src/tse/tse_python.cpp` and the LeakSanitizer suppressions deleted)
- Chain semantics in both dispatchers corrected and documented in `AGENTS.md`: chained prompts share the ORIGINAL piped stdin (nobody feeds a prompt's output into the next); a prompt that reads stdin (`read_stdin`/`embed_stdin`) spends the pipe for later prompts, reported natively via `prompt_output::stdin_consumed` (or `prompt_error::stdin_consumed()`)
- `bin/prompt auto` stock-identifier extraction bug fixed in `prompts/auto.sh`

### Removed

- Embedded CPython from `cpp_prompt` — the binary now shells out to the repo's shell/Python tooling the way the bash prompts do
- `cpp_prompt/src/legacy/` and `include/prompt/legacy/` (the bash-script fallback runner) — `cpp_prompt` no longer knows about or accesses the `prompts/` directory at all
- `prompt_descriptor::script`, `set_sort_file()` and `set_help_texts()` from the registry; `prompt_search_dirs()`/`find_prompt_file()`/`detect_file_type()` from `prompt::fs`

### Fixed

- Empty-but-piped stdin handling: `note` (and other stdin-reading prompts) print the same blank embed block as `embed_stdin || true` when the pipe is empty, keeping chained output byte-identical to bash
- `resolve_input_file` matches the bash implementation (cwd → git root → `git ls-files | fzf -f`)
