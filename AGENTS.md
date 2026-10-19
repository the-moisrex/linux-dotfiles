# AGENTS.md

Personal Linux dotfiles repo: shell utilities, AI prompt scripts, firewall configs, containerized services.

## What This Is

- Shell scripts (bash) are the primary language; a few Python scripts exist
- **Not** a software project — no build system, no linting, no tests for the repo itself
- MIT licensed, author: The Moisrex

## The Prompt System

The `prompt` command is a **native C++ dispatcher** — source in `prompts/`, installed to `bin/prompt` (see ["`prompts/` — native prompt dispatcher (C++)"](#prompts--native-prompt-dispatcher-c) below). It builds a fixed set of prompts into the binary and does **not** scan for `.sh`/`.txt`/`.md` files at run time. Output is auto-copied to the clipboard (a prompt opts out by setting `ctx.no_clipboard`).

```bash
prompt <name> [-- args...]       # run a prompt
prompt list                      # list prompts + short descriptions (--names for bare names)
prompt list-prompts              # print the full help of every prompt
cat file.cpp | prompt fix        # pipe input to a prompt
echo "task" | prompt auto        # auto-detect best prompt from input
```

`list` and `list-prompts` are ordinary native prompts (`prompts/src/prompts/list.cpp`, `list_prompts.cpp`), not special cases in the dispatcher.

Argument tokens that match a prompt name — optionally prefixed with `.` or `-` — start a chained prompt: `prompt stock فولاد .note "compare with فخوز"` runs `stock`, then `note` (`.files` embeds files generically, `.note` appends a note, `.cli` a CLI hint). Chained prompts do NOT receive the previous prompt's output on stdin — each sees the original piped stdin, which is spent once a prompt that reads stdin has consumed it (the dispatcher separates segments with blank lines and copies only the combined output).

The original **bash reference** implementation is kept under `prompts-shell/` (the dispatcher `prompts-shell/prompt.sh` plus the `*.sh` prompts). It is not installed — run it as `bash prompts-shell/prompt.sh <name>` — but stays functional for reference only; the native dispatcher is authoritative and no longer has to match it. Unlike the native dispatcher it also searches `$XDG_CONFIG_DIRS/prompts` and loads external `.sh`/`.txt`/`.md` files. The conventions and `_common.sh` API below describe those bash scripts.

**Prompt script conventions** (when editing the bash reference):
- Source `prompts-shell/_common.sh` and call `init_prompt` (or `init_prompt --no-files`), then `set -- "${ARGS[@]}"`
- Define `show_help()` with a `Usage: prompt <name>` line; its first paragraph must be a complete standalone summary (it is what `prompt list` shows)
- Use `infer_lang` for code block language tags, `trim_context` for `--head N` support
- Accept files as args and/or stdin; embed as fenced code blocks
- Print clear AI instructions first, then context
- Many prompts call `bin/` utilities (e.g., `spp`, `gtest-case`, `run`, `strip-osc`)

**Key prompts:**
- `auto` — dispatches to the right prompt based on input heuristics (compiler output, YouTube URLs, Dockerfile, CI config, language detection)
- `fix` / `review` / `tests` / `refactor` — general code analysis
- `cpp` / `cpp-reviewer` — C++ specific (auto-detects compiler errors)
- `run` — runs `bin/run`, embeds output for debugging
- `stock` — gathers TSETMC and Codal data via `bin/tse` (plus market context: USD/IRR, Iran inflation/GDP, large-cap breadth) for Iranian stock/fund analysis (`prompt stock فولاد`); accepts a Persian symbol, ISIN, insCode or easytrader/tsetmc/codal URL, falls back to the clipboard when omitted, full history by default (`--days N` to limit); one or more `.stock` snapshot files (written by `bin/tse.snapshot`) build the identical prompt from cached data — snapshot data goes through the same Markdown renderer as a live fetch (CSV history, compact sections) behind an `Instrument:` header — warning when older than 24 hours (`prompt stock stocks/14050717_فولاد_IRO1FOLD0009.stock`)
- `tse` — the same `bin/tse` collection as `stock` but with **no analysis instructions**: the raw Markdown dataset (live fetch or `.stock` snapshots), for piping or chaining into your own workflow (`prompt tse فولاد .note "compare with فخوز"`). Same flags, identifier forms, clipboard fallback and snapshot handling as `stock`; shares its data-collection code (`prompts/src/prompts/tse_data.hpp`). `stock` is `tse` + the institutional-analyst instructions
- `intraday` — same-day trade verdict (LONG or NO-TRADE with a numeric plan) from `bin/tse` data (`prompt intraday فولاد`); same identifier forms, clipboard fallback, full-history default and market context as `stock`
- `tse.rigor` — a market/TSE analysis and backtest honesty checklist (compute ratios from a table, no look-ahead bias, mandatory baseline/median/expectancy stats, explicit left-tail risk); standalone or chained (`prompt tse فولاد .tse.rigor`)
- `tse.times` — the pre-open and continuous-trading session times for every TSE/IFB instrument category (stocks, ETFs, fixed income, gold, certificates, energy, REITs, …) plus the Sat–Wed trading week; reference data, standalone or chained (`prompt tse فولاد .tse.times`)
- `tse.find` — turns a plain-language market request ("what to buy tomorrow") into a `bin/tse.find` screening command; chat mode (default) emits one runnable line, `--mode agentic` adds run-and-relax instructions (`--max-iter N`, `--dry-run`); embeds the live `tse.find --help` flags (`--head N` to cap); a `.tse` file, `tse.find` flags, or piped `tse.find` output (tse/json/jsonl/table) switches to review mode: rank the matches with their real numbers and suggest `prompt stock`/`prompt intraday` deep dives (`prompt tse.find findings.tse`)
- `gtest-case` / `gtest` — Google Test case source embedding
- `spp` — C++ symbol expansion via `bin/spp`
- `commit` — git commit message from staged/unstaged diff
- `new` — meta-prompt that generates a new native prompt (`prompts/src/prompts/<name>.cpp`); embeds the SDK headers plus the `fix` prompt as reference (C++-specific; the bash `new.sh` is not mirrored)
- `list` / `list-prompts` — prompt catalog: short descriptions / full help
- `prompt-compiler` — autocomplete/compiles prompts with `{{var}}` expansion and `/slash-commands`

**`prompts-shell/_common.sh` shared API:**
- `parse_arguments` — handles `--head N`, `--help`; remaining args go to `ARGS` array (`init_prompt` wraps it)
- `infer_lang <file>` — returns language name for syntax highlighting
- `trim_context <text>` — truncates to `$head_lines` if set
- `embed_file <path> [label]` — prints a fenced code block with language inference
- `resolve_input_file <name>` — resolves a filename via git root or fzf
- `select_files` — fzf multi-select from git-tracked files
- `collect_prompts` — prints `name<TAB>file` for every available prompt
- `prompt_search_dirs` — prints the directories searched for prompt files
- `extract_help para|full <files>` — statically extracts `show_help()` text via `prompts-shell/_extract-help.awk` (used by `list`/`list-prompts` to avoid running `bash --help` per prompt)

## `prompts/` — native prompt dispatcher (C++)

A native C++ replacement for the bash dispatcher under `prompts-shell/`. **Every prompt is ported natively** — `prompts/` has no runtime dependency on the bash scripts (it does not scan, read or execute any `.sh`/`.txt`/`.md` prompt). **One file = one prompt:** each `src/prompts/<name>.cpp` implements its prompt and registers itself, so there is no central list. The native dispatcher is now authoritative: the bash reference and its `~/parity/` harness are kept only as historical smoke tests, and deliberate divergences (the redaction legend, the native-only `tse`/`tse.rigor`, and the shared-help standardisation) are expected — there is no "keep it green" requirement any more. A subtree guide lives in `prompts/AGENTS.md`.

- Build: `cmake -S prompts -B prompts/build -DCMAKE_BUILD_TYPE=Release && cmake --build prompts/build -j 8` (the build type is not defaulted — a bare configure yields an unoptimised binary); Debug build with tests: `prompts/build-asan` (`ctest` there must pass)
- Install: `cmake --install prompts/build --prefix "$HOME/cmd" --component prompt` writes `bin/prompt` (gitignored; the `prompt` component keeps the FetchContent'd `httplib`/`date` out of the prefix). Because `~/cmd/bin` is on PATH, `prompt` then runs the native binary, and the executable's own directory is `bin/` — which is also how `fs::bin_tool` finds its sibling `bin/` tools without shelling out
- Format: clang-format (`clang-format -i` over `prompts/**/*.{cpp,hpp}`; `--dry-run -Werror` must be clean)
- Lint: clang-tidy via `.clang-tidy` (`clang-tidy -p build src/*.cpp src/*/*.cpp tests/*.cpp`); must report zero warnings. Its `--fix` mangles long single-line statements in headers, so fix one file at a time and compile after each
- Chain semantics match the bash dispatcher: prompts in a chain share the ORIGINAL piped stdin (nobody feeds a prompt's output into the next), segments are separated by blank lines, and a prompt that reads stdin (`read_stdin`/`embed_stdin`) spends the pipe for later prompts (reported through `prompt_context::stdin_consumed`, or `prompt_error::stdin_consumed()` when the prompt threw)
- Prompts that shell out (`run`, `spp`, `gtest-case`, `clang-tidy`, `git*`, `stock`/`intraday`/`tse*` via `bin/tse`, `skill`, `yt`, …) must match the script's exact output text and error routing (`tse: <msg>` exit 1, usage errors to stderr). Repo utilities are located with `fs::bin_tool` (exe dir → git root → cwd walk → `$PATH` resolved to an absolute path), never through the prompt scripts
- **Errors:** a prompt writes its result onto the `prompt_context` it was handed — `ctx.out` (stdout, sanitised/pipelined/clipboarded), `ctx.err` (stderr, exit stays 0) and `ctx.no_clipboard` — and *throws* on failure: `usage_error` (exit 2), `soft_error` (exit 1) or `prompt_error` (`exit_code`, `message` for stderr, `partial_output` for stdout printed before the failure, `no_clipboard`, `stdin_consumed`). The dispatcher catches them once in `main.cpp`'s chain loop (a failed prompt's partial stdout is still accumulated, the chain continues, first non-zero status wins). No prompt declares `noexcept`; functions that can throw say nothing.
- **Subprocesses:** `process::command` (fluent builder) or `run_command(argv, stdin, env, timeout)` — fork/exec, no shell, no quoting; `stderr_mode` picks inherited (bash's `$(...)`, the default) / captured / `2>&1`. `execvp`-style argv means an argument with spaces, quotes or newlines reaches the child intact. `run_script()` is the `/bin/bash -c` escape hatch. Tests: `tests/test_errors.cpp` (error surface), `tests/test_chaining.cpp`, `tests/test_fs.cpp` (`fs::bin_tool` layout/PATH resolution).
- **SDK helpers:** `src/sdk/args.hpp` carries the boilerplate the prompts used to repeat — `head_lines_or_throw(ctx, strict)` (the `--head` parse plus the `throw usage_error{…}` preamble) and `file_options_or_throw(ctx, strict)`, the file prompts' whole CLI: the validated `--head N` cap plus the value-less `--num`/`--numbered` line-number flag (spellings in `kNumberFlags`, tested with `is_number_flag(arg)`), returned as a `file_options {head_lines, numbered}`. `positional_args(ctx)` returns the arguments with the `--head <value>` pair and the `--num`/`--numbered` flags removed; `collect_file_args(ctx)` splits FILE arguments into the regular files to embed and the missing ones to warn about. `embed.hpp` has `number_lines(text, numbered)` (the `N | line` prefix `--num` enables) and the embed helpers take the `file_options` struct. Help text is generated from `file_usage_help(...)` / `file_options_help(...)` so every file prompt's usage and `--head`/`--num` block stay identical. `prompt_context` is a plain data bag; its service wrappers are free functions taking the context (`clear_output`, `clipboard_content`, `clipboard_identifier`, `select_files`, `resolve_input_file`), and `infer_lang` returns `std::string_view`
- **Redaction legend:** `sanitize::sanitize()` returns `{text, redacted}`, and when `clean_privacy` rewrites any personal data (`HOME`, `USER`, `HOSTNAME`, `IP_ADDRESS`, `MAC_ADDRESS`, `EMAIL_ADDRESS`, `PHONE_NUMBER`, `SSN`) `main.cpp` prepends one line (`sanitize::kRedactionLegend`) to the accumulated stdout and clipboard before printing, so the model knows the placeholder tokens are redactions, not the original text. This is a deliberate divergence from the bash reference (the bash reference has no such note). Tests: `tests/test_sanitize.cpp`
- **No embedded Python:** live TSE fetches shell out to `python3 bin/tse all … --markdown` (the tool's stderr is inherited, exactly like bash), `.stock` snapshots go through `bin/tse snapshot`/`bin/tse markdown` (the same renderer, so cached and live data match), skill frontmatter is parsed in C++, and the KDE clipboard path calls `qdbus` — nothing links libpython
- Adding a prompt: write `src/prompts/<name>.cpp` implementing `static void execute_<name>(prompt_context&)` and `static void render_help_<name>(std::ostream&)`, then end the file with one self-registration call — `bool const registered = prompt::register_prompt("<name>", "<summary>", &execute_<name>, &render_help_<name>);` (CMake globs the sources; no other file changes). The `<summary>` is the bash script's first help paragraph, verbatim, and is what `prompt list` prints. Registration runs at static-init time, which is why `prompt_lib` must stay an OBJECT library (a static archive would drop the otherwise-unreferenced registrar objects); the name is passed explicitly because dotted/dashed names (`git.diff`, `tse.find`, `fs8.mod`, `clang-tidy`) can't be derived from the identifier. `prompt list`/`--help`/`list-prompts` come straight from `render_help_fn`
- `prompt new` (C++-oriented, deliberately divergent from the bash `new.sh`) embeds the SDK headers and one example prompt so an AI can write a consistent new one. Those snapshots are **generated at build time** — `cmake/embed-assets.cmake` bakes the files listed in `PROMPT_ASSETS` (`CMakeLists.txt`) into `${CMAKE_BINARY_DIR}/generated/prompt_assets.hpp` as raw strings, so the binary never reads the source tree at run time yet can't go stale; add a header there (and to `PROMPT_ASSETS`) when the guidance should include it
- `list`/`list-prompts` ordering replays the bash reference dispatcher's `sort` over `name<TAB><repo>/prompts-shell/<name>.sh` keys (`assign_sort_files`); the path is a collation key only, never opened

## `bin/` Utilities (150+)

Each script is standalone. Check `bin/README.md` for the full categorized index. Key ones:

**C++ dev tools:**
- `spp` — parallel C++ symbol source extractor via clang (reads `.clang`/`.clangd` flags)
- `gtest-case` / `gtest-finder` — find Google Test case source from names
- `run` — find git root, locate CMake build dirs, run cmake targets
- `codeshell` — tmux-based IDE: editor + auto-recompiling side pane
- `clang.deps` — list a C++ file's dependencies
- `llvm.run` — run clang/LLVM plugins with project flags

**Dev containers:**
- `pod` — project dev containers: builds recipe-chained images from `pods/recipes/<name>` scripts (`base`, `dotfiles`, `oc`, `vscode`, `ssh`; each executable, prints a Dockerfile to stdout), runs labeled containers (bind/`--copy`/worktree workspaces) named `pod-<workspace>-<dish>`
- `dockerfile.gen` — standalone Dockerfile generator matching the host OS (used by `pod`'s `base` recipe when no Dockerfile exists)
- `oc` — opencode agent in a container (project mode delegates to `pod name` + `oc-seed`, otherwise the `pods/agents` image); `oc-seed` records/applies profile seeding on the host

**Prompt infrastructure:**
- `prompt` — the native prompt dispatcher (source `prompts/`, installed here as `bin/prompt`); `prompts-shell/` keeps the bash reference
- `prompt-compiler` — autocomplete and `{{var}}`/`/cmd` expansion engine
- `c.c` / `c.p` — clipboard copy/paste (Wayland/KDE/X11 aware)
- `strip-osc` — strip terminal escape sequences (used by prompt output pipeline)
- `clean.privacy` — redact personal info from text (used by prompt output pipeline)

**AI chat (web replay)** (`bin/ai/` package + thin entries):
- `ai.chatgpt` / `ai.grok` / `ai.dseek` — stream a site's chat answer by replaying its own web API with the browser's signed-in session (no API key, no browser at run time): prompt from args/stdin, one shared engine normalises `thinking`/`text`/`tool_call`/`usage`/`done`/`error`, Markdown on a TTY and clean stdout when piped, `--json` for one event per line. Tool calls are display-only, never executed. Providers: ChatGPT (`backend-api/f/conversation` conduit behind the sentinel gate — requirements token, `zvt` proof-of-work and the turnstile VM in `bin/ai/sentinel.py`), Grok (the `wss://grok.com/ws/mgw/` application WebSocket), DeepSeek (`DeepSeekHashV1` proof-of-work plus fragment-patch stream)
- `browser.session` — compose a site's authenticated session (cookies + web storage + UA) through `cookies`/`browser.storage`/`browser.find`; the sign-in source for `ai.*`
- `browser.record` — development-only Playwright recorder (requests, bodies, full SSE and WebSocket frames) used to derive the `ai.*` adapters

**Git tools:**
- `commit` — Python script for commit message generation (Conventional Commits)
- `gtask` — taskwarrior per-git-project
- `github.issues` / `github.repos` — GitHub API helpers

**Market data:**
- `tse` — TSETMC quotes, order book, flows, history, fundamentals, market context and Codal filings for `prompt stock`/`prompt intraday` (`tse all <symbol>` for JSON); dates and timestamps are Jalali (Shamsi), e.g. `1405/07/26 14:45:26`; `tse.find` screens the market, `tse.snapshot` stores fetches as `.stock` files (named `<JalaliYYYYMMDD>_<symbol>_<ISIN>.stock`), and `tse snapshot`/`tse markdown` read one back (metadata header + the rendered Markdown a live fetch would produce); identifiers are resolved through `tse.resolve` (finglish, fuzzy, composite `فولاد-IRO1FOLD0001` tokens, cached index)
- `tse.intraday` — intraday (per-minute) OHLCV bars for one instrument over one session or a range (`--days N`, capped at 30; `--date YYYY/MM/DD`; `--from`/`--to`), the live session counting as the most recent. A session is rebuilt from the per-trade tape (`Trade/GetTradeHistory`, or `Trade/GetTrade` for the live one; canceled rows dropped, rows deduplicated by trade number, trades sorted by time then number) and aggregated locally, with TSETMC's native 2-minute `Trade/GetTradeIntraDay` bars only as a live fallback (`source: live`). Consecutive identical-OHLC bars collapse to one `from`..`to` row, but only once a run exceeds `--collapse-threshold` (default 120 rows) so small/partial days keep every minute (`--collapse auto|on|off`, `--no-collapse` = `off`), and collapsed rows carry a `bars` column with the merged bar count. `--interval auto` (default) coarsens 1→2→3→5→6→10→15→30→60 minutes to fit `--max-rows` (default 2000), reporting the pick. The live session also prints the real-time `ClosingPriceInfo` quote and how far the CDN-cached `GetTrade` tape trails it. Each session also carries its daily حقیقی/حقوقی breakdown; `--ticks` prints the raw tape, `--profile` a price-level profile, `--json` JSON. Prices in rial, volume in shares, Jalali dates and `HH:MM` times; the CDN's gzip keeps a busy day ~0.5 MB on the wire; same identifier forms as `tse`; no cache
- `tse.resolve` — resolve Persian / finglish / fuzzy instrument names to TSETMC identifiers (symbol, ISIN, insCode, market/board) and cache the active-instrument index (`${XDG_CACHE_HOME:-~/.cache}/tse/instruments.json`, one week, `--refresh`); exact → finglish (`foolad`, `khodro`, phonetic `mobin` via `bin/finglish`) → fuzzy (`rapidfuzz`/`difflib`) with market-tier ranking so duplicated symbols pick the main-market listing (`فنفت`), composite `فولاد-IRO1FOLD0001` tokens accepted (`--complete` emits `finglish-ISIN` for Latin input and completes a partial ISIN); all `tse*` tools resolve through it
- `codal` — Codal.ir client behind `tse`'s Codal sections (also standalone: `codal search/statements/snapshot <symbol>`); solves search.codal.ir's rate-limit DDoS captcha with `tesseract` and keeps the cookie session that API requires, then extracts statement tables from `www.codal.ir` report pages

## Directory Layout

| Directory | Purpose |
|-----------|---------|
| `bin/` | 150+ standalone shell utilities |
| `prompts/` | Native C++ prompt dispatcher + prompt ports (the `prompt` command; installed to `bin/prompt`) |
| `prompts-shell/` | Bash reference prompt dispatcher (`prompt.sh`) + `*.sh` prompts, all source `_common.sh` (not installed; historical reference) |
| `firewall/` | nftables/iptables scripts and configs |
| `pods/` | Containerized services (podman-compose, managed by `pods/stack`) + dev-container recipes (`recipes/`) |
| `pkgs/` | Package lists (`pacman-core.txt`, `pacman-all.txt`, `dnf-core.txt`) + `core-map.txt`/`all-map.txt` (distro names with `NONE` placeholders for the dotfiles recipe and apt installs) |
| `setup/` | Modular setup scripts, all support `--uninstall --verbose --help` |
| `services/` | Systemd service files (system/ and user/) |
| `code-templates/` | Templates for `codeshell` (C, C++, Python, Assembly, etc.) |
| `configs/` | App configs (Alacritty themes, etc.) |
| `cleanup/` | Cleanup scripts (logs, apps, browsers, KDE, GNOME) |
| `vault/` | GPG-encrypted vault management |

## Gotchas

- `bin/` scripts have varying external dependencies — always check `--help` or header comments
- `prompt` auto-copies output to the clipboard; a native prompt opts out via `ctx.no_clipboard` (the bash reference by writing `no-clipboard` to `$PROMPT_NO_CLIPBOARD_FILE`)
- The bash reference prompt scripts are not executable (run via `bash prompts-shell/prompt.sh <name>`); the native dispatcher does not use them at all
- `prompt-compiler` needs Python 3 and shells out to `prompt list --names` for autocomplete
- `spp` needs `clang` and reads `.clang`/`.clangd` from the git root
- Codal fetches (`tse codal`/`tse all`, `codal`) solve search.codal.ir's rate-limit captcha with `tesseract` (English traineddata); without it they fail with an install hint, and the search API needs its cookie session or it returns zero rows silently
- `pods/stack start` sets `net.ipv4.ip_unprivileged_port_start=80` via sudo
- Firewall scripts need root and use nftables
- `transfer` service (transfer.sh) is currently down
