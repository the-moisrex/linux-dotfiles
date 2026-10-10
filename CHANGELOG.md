# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `cpp_prompt/` — native C++ prompt dispatcher and prompt ports, kept behaviorally identical (stdout, stderr, exit codes) to the bash side (`bin/prompt` + `prompts/*.sh`), with a parity harness comparing both sides
- Native ports for `run`, `new`, `note`, `files`, `auto`, `cpp`, `cpp-reviewer`, `gtest-case`, `commit`, `stock`, `intraday`, `tse.find` (the last delegates to the legacy script runner), plus the shared dispatch/chaining/pipeline/clipboard/sanitize SDK

### Changed

- Chain semantics in both dispatchers corrected and documented in `AGENTS.md`: chained prompts share the ORIGINAL piped stdin (nobody feeds a prompt's output into the next); a prompt that reads stdin (`read_stdin`/`embed_stdin`) spends the pipe for later prompts, reported natively via `prompt_result.stdin_consumed`
- `process::run_command` now single-quote-escapes child arguments (fixes double-quote corruption of paths with spaces)
- `bin/prompt auto` stock-identifier extraction bug fixed in `prompts/auto.sh`

### Fixed

- Empty-but-piped stdin handling: `note` (and other stdin-reading prompts) print the same blank embed block as `embed_stdin || true` when the pipe is empty, keeping chained output byte-identical to bash
- `resolve_input_file` matches the bash implementation (cwd → git root → `git ls-files | fzf -f`)
