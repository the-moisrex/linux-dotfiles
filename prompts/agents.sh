#!/usr/bin/env bash
set -euo pipefail

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

NO_FILES=true
source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

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

found=0

for name in "${agent_files[@]}"; do
    path="$GIT_ROOT/$name"
    if [[ -f "$path" ]]; then
        embed_file "$path"
        echo
        found=1
    fi
done

if [[ -d "$GIT_ROOT/.opencode" ]]; then
    while IFS= read -r -d '' mdfile; do
        embed_file "$mdfile"
        echo
        found=1
    done < <(find "$GIT_ROOT/.opencode" -maxdepth 1 -name '*.md' -print0 2>/dev/null || true)
fi
