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

The original **bash reference** implementation is kept under `prompts-shell/` (the dispatcher `prompts-shell/prompt.sh` plus the `*.sh` prompts). It is not installed — run it as `bash prompts-shell/prompt.sh <name>` — but stays functional for reference and is what the parity harness compares against; unlike the native dispatcher it also searches `$XDG_CONFIG_DIRS/prompts` and loads external `.sh`/`.txt`/`.md` files. The conventions and `_common.sh` API below describe those bash scripts.

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
- `stock` — gathers TSETMC and Codal data via `bin/tse` (plus market context: USD/IRR, Iran inflation/GDP, large-cap breadth) for Iranian stock/fund analysis (`prompt stock فولاد`); accepts a Persian symbol, ISIN, insCode or easytrader/tsetmc/codal URL, falls back to the clipboard when omitted, full history by default (`--days N` to limit); one or more `.stock` snapshot files (written by `bin/tse.snapshot`) build the identical prompt from cached data — snapshot data goes through the same Markdown renderer as a live fetch (CSV history, compact sections) behind an `Instrument:` header — warning when older than 24 hours (`prompt stock stocks/20261009_فولاد_IRO1FOLD0009.stock`)
- `intraday` — same-day trade verdict (LONG or NO-TRADE with a numeric plan) from `bin/tse` data (`prompt intraday فولاد`); same identifier forms, clipboard fallback, full-history default and market context as `stock`
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

A native C++ replacement for the bash dispatcher under `prompts-shell/`. **Every prompt is ported natively** — `prompts/` has no runtime dependency on the bash scripts (it does not scan, read or execute any `.sh`/`.txt`/`.md` prompt). **One file = one prompt:** each `src/prompts/<name>.cpp` implements its prompt and registers itself, so there is no central list. Help text, output bytes and exit codes stay identical to the bash side (except `new`, which is deliberately C++-specific — it generates a native `src/prompts/<name>.cpp`, not a bash script); the parity harness (case lists + `parity.sh` under `~/parity/`) compares both sides (`bash prompts-shell/prompt.sh` vs `prompts/build/prompt`) — keep it green when touching either side. `new` (and the bare catalog forms of `list`/`list-prompts`/`ask-for-help`, which render its text) are excluded there.

- Build: `cmake -S prompts -B prompts/build && cmake --build prompts/build -j 8`; Debug build with tests: `prompts/build-asan` (`ctest` there must pass)
- Install: `cmake --install prompts/build --prefix "$HOME/cmd" --component prompt` writes `bin/prompt` (gitignored; the `prompt` component keeps the FetchContent'd `httplib`/`date` out of the prefix). Because `~/cmd/bin` is on PATH, `prompt` then runs the native binary, and the executable's own directory is `bin/` — which is also how `fs::bin_tool` finds its sibling `bin/` tools without shelling out
- Format: clang-format (`clang-format -i` over `prompts/**/*.{cpp,hpp}`; `--dry-run -Werror` must be clean)
- Lint: clang-tidy via `.clang-tidy` (`clang-tidy -p build src/*.cpp src/*/*.cpp tests/*.cpp`); must report zero warnings. Its `--fix` mangles long single-line statements in headers, so fix one file at a time and compile after each
- Chain semantics match the bash dispatcher: prompts in a chain share the ORIGINAL piped stdin (nobody feeds a prompt's output into the next), segments are separated by blank lines, and a prompt that reads stdin (`read_stdin`/`embed_stdin`) spends the pipe for later prompts (reported through `prompt_context::stdin_consumed`, or `prompt_error::stdin_consumed()` when the prompt threw)
- Prompts that shell out (`run`, `spp`, `gtest-case`, `clang-tidy`, `git*`, `stock`/`intraday`/`tse*` via `bin/tse`, `skill`, `yt`, …) must match the script's exact output text and error routing (`tse: <msg>` exit 1, usage errors to stderr). Repo utilities are located with `fs::bin_tool` (exe dir → git root → cwd walk → PATH), never through the prompt scripts
- **Errors:** a prompt writes its result onto the `prompt_context` it was handed — `ctx.out` (stdout, sanitised/pipelined/clipboarded), `ctx.err` (stderr, exit stays 0) and `ctx.no_clipboard` — and *throws* on failure: `usage_error` (exit 2), `soft_error` (exit 1) or `prompt_error` (`exit_code`, `message` for stderr, `partial_output` for stdout printed before the failure, `no_clipboard`, `stdin_consumed`). The dispatcher catches them once in `main.cpp`'s chain loop (a failed prompt's partial stdout is still accumulated, the chain continues, first non-zero status wins). No prompt declares `noexcept`; functions that can throw say nothing.
- **Subprocesses:** `process::command` (fluent builder) or `run_command(argv, stdin, env, timeout)` — fork/exec, no shell, no quoting; `stderr_mode` picks inherited (bash's `$(...)`, the default) / captured / `2>&1`. `execvp`-style argv means an argument with spaces, quotes or newlines reaches the child intact. `run_script()` is the `/bin/bash -c` escape hatch. Tests: `tests/test_errors.cpp` (error surface), `tests/test_chaining.cpp`.
- **No embedded Python:** live TSE fetches shell out to `python3 bin/tse all … --markdown` (the tool's stderr is inherited, exactly like bash), `.stock` snapshots go through `bin/tse snapshot`/`bin/tse markdown` (the same renderer, so cached and live data match), skill frontmatter is parsed in C++, and the KDE clipboard path calls `qdbus` — nothing links libpython
- Adding a prompt: write `src/prompts/<name>.cpp` implementing `static void execute_<name>(prompt_context&)` and `static void render_help_<name>(std::ostream&)`, then end the file with one self-registration call — `bool const registered = prompt::register_prompt("<name>", "<summary>", &execute_<name>, &render_help_<name>);` (CMake globs the sources; no other file changes). The `<summary>` is the bash script's first help paragraph, verbatim, and is what `prompt list` prints. Registration runs at static-init time, which is why `prompt_lib` must stay an OBJECT library (a static archive would drop the otherwise-unreferenced registrar objects); the name is passed explicitly because dotted/dashed names (`git.diff`, `tse.find`, `fs8.mod`, `clang-tidy`) can't be derived from the identifier. Re-run the parity harness after adding one. `prompt list`/`--help`/`list-prompts` come straight from `render_help_fn`
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

**Git tools:**
- `commit` — Python script for commit message generation (Conventional Commits)
- `gtask` — taskwarrior per-git-project
- `github.issues` / `github.repos` — GitHub API helpers

**Market data:**
- `tse` — TSETMC quotes, order book, flows, history, fundamentals, market context and Codal filings for `prompt stock`/`prompt intraday` (`tse all <symbol>` for JSON); `tse.find` screens the market, `tse.snapshot` stores fetches as `.stock` files, and `tse snapshot`/`tse markdown` read one back (metadata header + the rendered Markdown a live fetch would produce)
- `codal` — Codal.ir client behind `tse`'s Codal sections (also standalone: `codal search/statements/snapshot <symbol>`); solves search.codal.ir's rate-limit DDoS captcha with `tesseract` and keeps the cookie session that API requires, then extracts statement tables from `www.codal.ir` report pages

## Directory Layout

| Directory | Purpose |
|-----------|---------|
| `bin/` | 150+ standalone shell utilities |
| `prompts/` | Native C++ prompt dispatcher + prompt ports (the `prompt` command; installed to `bin/prompt`) |
| `prompts-shell/` | Bash reference prompt dispatcher (`prompt.sh`) + `*.sh` prompts, all source `_common.sh` (not installed; parity reference) |
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
