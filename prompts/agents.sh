#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt agents [--head N]

Find and embed agent instruction files (AGENTS.md, CLAUDE.md, etc.) from
the current Git repository root.  This gives the AI project conventions,
coding standards, and architectural context before it answers a question.

Searches the git root for any of these files:
  AGENTS.md, CLAUDE.md, COPILOT.md, .cursorrules,
  .github/copilot-instructions.md, CONVENTIONS.md, ARCHITECTURE.md,
  and any *.md inside .opencode/

Options:
  --head N   Keep only the first N lines of each embedded file
  -h, --help Show this help
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

find_git_root

if [[ -z "$GIT_ROOT" ]]; then
    echo "Error: not inside a git repository." >&2
    exit 1
fi

agent_files=(
    AGENTS.md
    CLAUDE.md
    COPILOT.md
    .cursorrules
    .github/copilot-instructions.md
    CONVENTIONS.md
    ARCHITECTURE.md
)

# Embed a file using plain text to avoid nested markdown fences
# (AGENTS.md often contains ``` blocks that break when wrapped in ```markdown)
embed_agent_file() {
    local path="$1"
    local name
    name="$(basename "$path")"
    echo
    echo "File: $name"
    echo '```text'
    trim_context "$(cat -- "$path")"
    echo '```'
}

# Dedup by target inode: names that resolve to the same file (symlinks like
# CLAUDE.md -> AGENTS.md, hardlinks, .opencode/AGENTS.md -> ../AGENTS.md)
# are embedded only once, silently.  Also skips broken symlinks and
# non-regular files so `cat` never runs on them.
declare -A seen_files=()

maybe_embed() {
    local path="$1" key
    [[ -f "$path" ]] || return 1
    key="$(stat -Lc '%d:%i' -- "$path" 2>/dev/null)" || return 1
    [[ -n "${seen_files[$key]:-}" ]] && return 1
    seen_files["$key"]=1
    embed_agent_file "$path"
}

found=0

for name in "${agent_files[@]}"; do
    path="$GIT_ROOT/$name"
    if maybe_embed "$path"; then
        found=1
    fi
done

if [[ -d "$GIT_ROOT/.opencode" ]]; then
    while IFS= read -r -d '' mdfile; do
        if maybe_embed "$mdfile"; then
            found=1
        fi
    done < <(find -L "$GIT_ROOT/.opencode" -maxdepth 1 -name '*.md' -type f -print0 2>/dev/null | sort -z || true)
fi
