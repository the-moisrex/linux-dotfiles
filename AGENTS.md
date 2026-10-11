# AGENTS.md

Personal Linux dotfiles repo: shell utilities, AI prompt scripts, firewall configs, containerized services.

## What This Is

- Shell scripts (bash) are the primary language; a few Python scripts exist
- **Not** a software project — no build system, no linting, no tests for the repo itself
- MIT licensed, author: The Moisrex

## The Prompt System (`prompts/`)

The `prompt` dispatcher (`bin/prompt`) searches `$XDG_CONFIG_DIRS/prompts` and `prompts/` for prompt files (`.sh`, `.txt`, `.md`). It auto-copies output to clipboard (a prompt script opts out by writing `no-clipboard` to `$PROMPT_NO_CLIPBOARD_FILE`).

```bash
prompt <name> [-- args...]       # run a prompt
prompt list                      # list prompts + short descriptions (--names for bare names)
prompt list-prompts              # print the full help of every prompt
cat file.cpp | prompt fix        # pipe input to a prompt
echo "task" | prompt auto        # auto-detect best prompt from input
```

`list` and `list-prompts` are ordinary prompt scripts (`prompts/list.sh`, `prompts/list-prompts.sh`), not special cases in the dispatcher.

Argument tokens that match a prompt name — optionally prefixed with `.` or `-` — start a chained prompt: `prompt stock فولاد .note "compare with فخوز"` runs `stock`, then `note` (`.files` embeds files generically, `.note` appends a note, `.cli` a CLI hint). Chained prompts do NOT receive the previous prompt's output on stdin — each sees the original piped stdin, which is spent once a prompt that reads stdin has consumed it (the dispatcher separates segments with blank lines and copies only the combined output).

**Prompt script conventions** (follow these when adding/editing prompts):
- Source `prompts/_common.sh` and call `init_prompt` (or `init_prompt --no-files`), then `set -- "${ARGS[@]}"`
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
- `new` — meta-prompt that generates new prompt scripts (embeds `_common.sh` as reference)
- `list` / `list-prompts` — prompt catalog: short descriptions / full help
- `prompt-compiler` — autocomplete/compiles prompts with `{{var}}` expansion and `/slash-commands`

**`prompts/_common.sh` shared API:**
- `parse_arguments` — handles `--head N`, `--help`; remaining args go to `ARGS` array (`init_prompt` wraps it)
- `infer_lang <file>` — returns language name for syntax highlighting
- `trim_context <text>` — truncates to `$head_lines` if set
- `embed_file <path> [label]` — prints a fenced code block with language inference
- `resolve_input_file <name>` — resolves a filename via git root or fzf
- `select_files` — fzf multi-select from git-tracked files
- `collect_prompts` — prints `name<TAB>file` for every available prompt
- `prompt_search_dirs` — prints the directories searched for prompt files
- `extract_help para|full <files>` — statically extracts `show_help()` text via `prompts/_extract-help.awk` (used by `list`/`list-prompts` to avoid running `bash --help` per prompt)

## `cpp_prompt/` — native prompt dispatcher (C++)

A native C++ reimplementation of `bin/prompt` + `prompts/*.sh`. **Every prompt is ported natively** — `cpp_prompt/` has no runtime dependency on the `prompts/` directory (it does not scan, read or execute any `.sh`/`.txt`/`.md` prompt; the static snapshots `new` embeds live in `prompt/prompts/prompt_assets.hpp`). Help text, output bytes and exit codes stay identical to the bash side; the parity harness (case lists + `parity.sh` under `/home/moisrex/.tmp/opencode/`) compares both sides — keep it green when touching either side.

- Build: `cmake -S cpp_prompt -B cpp_prompt/build && cmake --build cpp_prompt/build -j 8`; Debug build with tests: `cpp_prompt/build-asan` (`ctest` there must pass)
- Format: clang-format (`clang-format -i` over `cpp_prompt/**/*.{cpp,hpp}`; `--dry-run -Werror` must be clean)
- Chain semantics match `bin/prompt`: prompts in a chain share the ORIGINAL piped stdin (nobody feeds a prompt's output into the next), segments are separated by blank lines, and a prompt that reads stdin (`read_stdin`/`embed_stdin`) spends the pipe for later prompts (native prompts report `stdin_consumed` in `prompt_result`)
- Prompts that shell out (`run`, `spp`, `gtest-case`, `clang-tidy`, `git*`, `stock`/`intraday`/`tse*` via `bin/tse`, `skill`, `yt`, …) must match the script's exact output text and error routing (`tse: <msg>` exit 1, usage errors to stderr). Repo utilities are located with `fs::bin_tool` (exe dir → git root → cwd walk → PATH), never through the prompt scripts
- Adding a prompt: drop a *_prompt.cpp/**_prompt.hpp pair into `cpp_prompt/src/prompts/` + `include/prompt/prompts/` (CMake globs the sources), register it in one of the `register_batch_*.cpp` files, and re-run the parity harness. `prompt list`/`--help`/`list-prompts` come straight from the registry (`help_summary` = the bash script's first help paragraph, verbatim)
- `list`/`list-prompts` ordering replays the bash dispatcher's `sort` over `name<TAB><repo>/prompts/<name>.sh` keys (`assign_sort_files`); the path is a collation key only, never opened

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
- `prompt` — the prompt dispatcher (searches XDG + repo prompts dir)
- `prompt-compiler` — autocomplete and `{{var}}`/`/cmd` expansion engine
- `c.c` / `c.p` — clipboard copy/paste (Wayland/KDE/X11 aware)
- `strip-osc` — strip terminal escape sequences (used by prompt output pipeline)
- `clean.privacy` — redact personal info from text (used by prompt output pipeline)

**Git tools:**
- `commit` — Python script for commit message generation (Conventional Commits)
- `gtask` — taskwarrior per-git-project
- `github.issues` / `github.repos` — GitHub API helpers

**Market data:**
- `tse` — TSETMC quotes, order book, flows, history, fundamentals, market context and Codal filings for `prompt stock`/`prompt intraday` (`tse all <symbol>` for JSON); `tse.find` screens the market, `tse.snapshot` stores fetches as `.stock` files
- `codal` — Codal.ir client behind `tse`'s Codal sections (also standalone: `codal search/statements/snapshot <symbol>`); solves search.codal.ir's rate-limit DDoS captcha with `tesseract` and keeps the cookie session that API requires, then extracts statement tables from `www.codal.ir` report pages

## Directory Layout

| Directory | Purpose |
|-----------|---------|
| `bin/` | 150+ standalone shell utilities |
| `prompts/` | AI prompt scripts (`.sh`), all source `_common.sh` |
| `cpp_prompt/` | Native C++ prompt dispatcher + prompt ports (behaviorally identical to `bin/prompt` + `prompts/*.sh`) |
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
- `prompt` auto-copies output to the clipboard; a prompt script opts out by writing `no-clipboard` to `$PROMPT_NO_CLIPBOARD_FILE`
- Prompt scripts are not executable (run via `bash`); the dispatcher handles this
- `prompt-compiler` needs Python 3 and shells out to `prompt list --names` for autocomplete
- `spp` needs `clang` and reads `.clang`/`.clangd` from the git root
- Codal fetches (`tse codal`/`tse all`, `codal`) solve search.codal.ir's rate-limit captcha with `tesseract` (English traineddata); without it they fail with an install hint, and the search API needs its cookie session or it returns zero rows silently
- `pods/stack start` sets `net.ipv4.ip_unprivileged_port_start=80` via sudo
- Firewall scripts need root and use nftables
- `transfer` service (transfer.sh) is currently down
