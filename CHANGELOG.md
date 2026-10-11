# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `cpp_prompt/` — native C++ prompt dispatcher with **all prompts implemented natively**: no runtime dependency on the `prompts/` directory (no scanning/reading/executing of prompt scripts; `new` embeds static snapshots instead). Every prompt's stdout, stderr, exit codes, `--help` text and `prompt list` summary stay byte-identical to `bin/prompt` + `prompts/*.sh`
- Native ports for the remaining prompt families: text/analysis (`api`, `explain`, `debug`, `diagram`, `metadata`, …), git (`git`, `git.diff`, `git-dirty`, `git.files`, `git.worktree.*`, `repo`, `verify`), tool wrappers (`clang-tidy`, `cmake`, `gdb`, `cppman`, `ci`, `docker`, `gh.issue`, `paths`, …), stdin/translate (`english`, `farsi`, `stdin`, `tweets`, `symbols`, `summarize`, `yt`), complex (`agents`, `ai-said`, `ask-for-help`, `skill`, `opportunities`, `fs8.mod`, `fs8.test`), plus the shared dispatch/chaining/pipeline/clipboard/sanitize SDK
- `prompt::fs::bin_tool` — locates repo `bin/` utilities (exe dir → git root → cwd walk → PATH) without going through the prompt scripts
- `prompt/prompts/prompt_assets.hpp` — static snapshots of `_common.sh`/`fix.sh`/`symbols.sh` embedded into the binary for the `new` prompt

### Removed

- `cpp_prompt/src/legacy/` and `include/prompt/legacy/` (the bash-script fallback runner) — `cpp_prompt` no longer knows about or accesses the `prompts/` directory at all
- `prompt_descriptor::script`, `set_sort_file()` and `set_help_texts()` from the registry; `prompt_search_dirs()`/`find_prompt_file()`/`detect_file_type()` from `prompt::fs`

### Changed

- Chain semantics in both dispatchers corrected and documented in `AGENTS.md`: chained prompts share the ORIGINAL piped stdin (nobody feeds a prompt's output into the next); a prompt that reads stdin (`read_stdin`/`embed_stdin`) spends the pipe for later prompts, reported natively via `prompt_result.stdin_consumed`
- `process::run_command` now single-quote-escapes child arguments (fixes double-quote corruption of paths with spaces)
- `bin/prompt auto` stock-identifier extraction bug fixed in `prompts/auto.sh`

### Fixed

- Empty-but-piped stdin handling: `note` (and other stdin-reading prompts) print the same blank embed block as `embed_stdin || true` when the pipe is empty, keeping chained output byte-identical to bash
- `resolve_input_file` matches the bash implementation (cwd → git root → `git ls-files | fzf -f`)
