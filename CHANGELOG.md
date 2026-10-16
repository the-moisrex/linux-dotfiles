# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]


### Added

- `bin/tse` history: every daily row now carries the historical حقیقی/حقوقی breakdown from `ClientType/GetClientTypeHistory` (the API behind tsetmc.com's `/History/<insCode>/<date>` page) — `individual_buy_shares`, `individual_sell_shares`, `legal_buy_shares`, `legal_sell_shares`, `legal_net_shares`, `individual_buyers`, `individual_sellers`, `legal_buyers`, `legal_sellers` — joined by date alongside price/volume/trade count in the CSV table, `tse history --json`/`--unadjusted` and `tse all --markdown`. The client-type series starts 2018-11-26 and a few later sessions are missing upstream, so those cells stay empty (missing = unknown, not zero); non-trading days also have no record. A failed client-type fetch no longer fails the price table, it is reported under `errors.client_type_history`
- `bin/tse` `snapshot FILE` and `markdown FILE` subcommands — print a `.stock` snapshot's 4-line metadata header, and render it to the same Markdown a live `tse all --markdown` fetch produces (CSV history, compact sections, `Instrument:` header). `prompt stock` (both `prompts/stock.sh` and the C++ port) builds snapshot context through them, so a cached snapshot and a live fetch share one renderer
- `process::command` — fluent subprocess builder (fork/exec, `stderr_mode` inherited/captured/`2>&1`, timeout, env) plus `run_script()` for `/bin/bash -c`; the dead `run_pipeline()`/`run_async()` wrappers are gone
- `prompt::text` string utilities (`core/strings.hpp`), `embed_file_block()`, `warn_missing_file()`, `help_text()` — the repeated file-embedding, warning and help-capture patterns every prompt used to open-code
- `cpp_prompt/tests/test_errors.cpp` — the `prompt_error` exception surface (exit codes, partial stdout, clipboard opt-out, stdin-spent flag) that replaced the success-return struct
- `cpp_prompt/` — native C++ prompt dispatcher with **all prompts implemented natively**: no runtime dependency on the `prompts/` directory (no scanning/reading/executing of prompt scripts; `new` embeds static snapshots instead). Every prompt's stdout, stderr, exit codes, `--help` text and `prompt list` summary stay byte-identical to `bin/prompt` + `prompts/*.sh`
- Native ports for the remaining prompt families: text/analysis (`api`, `explain`, `debug`, `diagram`, `metadata`, …), git (`git`, `git.diff`, `git-dirty`, `git.files`, `git.worktree.*`, `repo`, `verify`), tool wrappers (`clang-tidy`, `cmake`, `gdb`, `cppman`, `ci`, `docker`, `gh.issue`, `paths`, …), stdin/translate (`english`, `farsi`, `stdin`, `tweets`, `symbols`, `summarize`, `yt`), complex (`agents`, `ai-said`, `ask-for-help`, `skill`, `opportunities`, `fs8.mod`, `fs8.test`), plus the shared dispatch/chaining/pipeline/clipboard/sanitize SDK
- `prompt::fs::bin_tool` — locates repo `bin/` utilities (exe dir → git root → cwd walk → PATH) without going through the prompt scripts
- `cpp_prompt/src/prompts/prompt_assets.hpp` — static snapshots of `_common.sh`/`fix.sh`/`symbols.sh` embedded into the binary for the `new` prompt

### Changed

- `cpp_prompt/` structure: a prompt is one file that implements and registers itself — `static void execute_<name>(prompt_context&)` writes `ctx.out`/`ctx.err` and **throws** on failure (`usage_error`/`soft_error`/`prompt_error` in `sdk/errors.hpp`); the dispatcher catches them once in `main.cpp`'s chain loop. The `prompt_output` return struct, the 76 per-prompt header pairs, the umbrella `prompts/prompts.hpp` and the central `prompt_entry.cpp`/`register_batch_*.cpp` registrars are all gone; each `src/prompts/<name>.cpp` ends with a single `prompt::register_prompt(...)` call at static-init time (hence `prompt_lib` is an OBJECT library, so the registrars aren't dropped)
- Subprocesses run through fork/exec instead of `popen` + shell-quoting: argv reaches the child verbatim, child stderr can be captured or merged, and timeouts are enforced
- Repeated `output += "literal" + x + "literal"` sites (143) converted to `std::format`; lying `noexcept` qualifiers removed (they aborted on `std::stoull`/`std::regex` failures) and the numeric parsing hardened
- `stock`/`intraday` fetch through `python3 bin/tse … --markdown` exactly like `stock.sh` (the tool's stderr is inherited); skill frontmatter parsing and the KDE clipboard path are C++/`qdbus` — `cpp_prompt` no longer embeds or links libpython (`src/core/python.cpp`, `src/tse/tse_python.cpp` and the LeakSanitizer suppressions deleted)
- Chain semantics in both dispatchers corrected and documented in `AGENTS.md`: chained prompts share the ORIGINAL piped stdin (nobody feeds a prompt's output into the next); a prompt that reads stdin (`read_stdin`/`embed_stdin`) spends the pipe for later prompts, reported natively via `prompt_context::stdin_consumed` (or `prompt_error::stdin_consumed()`)
- `bin/prompt auto` stock-identifier extraction bug fixed in `prompts/auto.sh`

### Removed

- Embedded CPython from `cpp_prompt` — the binary now shells out to the repo's shell/Python tooling the way the bash prompts do
- `cpp_prompt/src/legacy/` and `include/prompt/legacy/` (the bash-script fallback runner) — `cpp_prompt` no longer knows about or accesses the `prompts/` directory at all
- `prompt_descriptor::script`, `set_sort_file()` and `set_help_texts()` from the registry; `prompt_search_dirs()`/`find_prompt_file()`/`detect_file_type()` from `prompt::fs`
- The C++ `.stock` snapshot renderer (`cpp_prompt/src/tse/`, including `tse_markdown.cpp`) — snapshots now render through `bin/tse markdown`, so live fetches and cached snapshots share the one implementation in `bin/tse` (the C++ port had drifted from its column set)

### Fixed

- Empty-but-piped stdin handling: `note` (and other stdin-reading prompts) print the same blank embed block as `embed_stdin || true` when the pipe is empty, keeping chained output byte-identical to bash
- `resolve_input_file` matches the bash implementation (cwd → git root → `git ls-files | fzf -f`)
- `prompt stock` in the C++ dispatcher honours `--head`: it was parsed but never applied, so `prompt stock --head N <snapshot>` printed the full rendered snapshot instead of the capped one
- Debug-build `-Wall -Wextra -Wshadow` warnings from `process.cpp` (unused `quote_for`) and `skill.cpp` (a shadowed loop variable) cleared
