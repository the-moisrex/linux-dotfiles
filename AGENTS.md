# AGENTS.md

Personal Linux dotfiles repo: shell utilities, AI prompt scripts, firewall configs, containerized services.

## What This Is

- Shell scripts (bash) are the primary language; a few Python scripts exist
- **Not** a software project — no build system, no linting, no tests for the repo itself
- MIT licensed, author: The Moisrex

## The Prompt System (`prompts/`)

The `prompt` dispatcher (`bin/prompt`) searches `$XDG_CONFIG_DIRS/prompts` and `prompts/` for prompt files (`.sh`, `.txt`, `.md`). It auto-copies output to clipboard when stdout is a terminal.

```bash
prompt <name> [-- args...]       # run a prompt
prompt list                      # list available prompts with descriptions
cat file.cpp | prompt fix        # pipe input to a prompt
echo "task" | prompt auto        # auto-detect best prompt from input
```

**Prompt script conventions** (follow these when adding/editing prompts):
- Source `prompts/_common.sh` and call `common_behavior`, then `set -- "${ARGS[@]}"`
- Define `show_help()` with a `Usage: prompt <name>` line
- Use `infer_lang` for code block language tags, `trim_context` for `--head N` support
- Accept files as args and/or stdin; embed as fenced code blocks
- Print clear AI instructions first, then context
- Many prompts call `bin/` utilities (e.g., `spp`, `gtest-case`, `run`, `strip-osc`)

**Key prompts:**
- `auto` — dispatches to the right prompt based on input heuristics (compiler output, YouTube URLs, Dockerfile, CI config, language detection)
- `fix` / `review` / `tests` / `refactor` — general code analysis
- `cpp` / `cpp-reviewer` — C++ specific (auto-detects compiler errors)
- `run` — runs `bin/run`, embeds output for debugging
- `gtest-case` / `gtest` — Google Test case source embedding
- `spp` — C++ symbol expansion via `bin/spp`
- `commit` — git commit message from staged/unstaged diff
- `new` — meta-prompt that generates new prompt scripts (embeds `_common.sh` as reference)
- `prompt-compiler` — autocomplete/compiles prompts with `{{var}}` expansion and `/slash-commands`

**`prompts/_common.sh` shared API:**
- `common_behavior` = `parse_arguments` + `print_stdin`
- `print_stdin` — reads stdin or uses fzf for file selection
- `parse_arguments` — handles `--head N`, `--help`; remaining args go to `ARGS` array
- `infer_lang <file>` — returns language name for syntax highlighting
- `trim_context <text>` — truncates to `$head_lines` if set
- `embed_file <path> [label]` — prints a fenced code block with language inference
- `resolve_input_file <name>` — resolves a filename via git root or fzf
- `select_files` — fzf multi-select from git-tracked files

## `bin/` Utilities (150+)

Each script is standalone. Check `bin/README.md` for the full categorized index. Key ones:

**C++ dev tools:**
- `spp` — parallel C++ symbol source extractor via clang (reads `.clang`/`.clangd` flags)
- `gtest-case` / `gtest-finder` — find Google Test case source from names
- `run` — find git root, locate CMake build dirs, run cmake targets
- `codeshell` — tmux-based IDE: editor + auto-recompiling side pane
- `clang.deps` — list a C++ file's dependencies
- `llvm.run` — run clang/LLVM plugins with project flags

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

## Directory Layout

| Directory | Purpose |
|-----------|---------|
| `bin/` | 150+ standalone shell utilities |
| `prompts/` | AI prompt scripts (`.sh`), all source `_common.sh` |
| `firewall/` | nftables/iptables scripts and configs |
| `pods/` | Containerized services (podman-compose), managed by `pods/pod` |
| `pkgs/` | Package lists (`pacman-core.txt`, `pacman-all.txt`, `dnf-core.txt`) |
| `setup/` | Modular setup scripts, all support `--uninstall --verbose --help` |
| `services/` | Systemd service files (system/ and user/) |
| `code-templates/` | Templates for `codeshell` (C, C++, Python, Assembly, etc.) |
| `configs/` | App configs (Alacritty themes, etc.) |
| `cleanup/` | Cleanup scripts (logs, apps, browsers, KDE, GNOME) |
| `vault/` | GPG-encrypted vault management |

## Gotchas

- `bin/` scripts have varying external dependencies — always check `--help` or header comments
- `prompt` auto-copies to clipboard when stdout is a terminal; pipe to something to avoid this
- Prompt scripts are not executable (run via `bash`); the dispatcher handles this
- `prompt-compiler` needs Python 3 and shells out to `prompt list-prompts` for autocomplete
- `spp` needs `clang` and reads `.clang`/`.clangd` from the git root
- `pods/pod start` sets `net.ipv4.ip_unprivileged_port_start=80` via sudo
- Firewall scripts need root and use nftables
- `transfer` service (transfer.sh) is currently down
