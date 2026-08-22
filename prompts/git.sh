#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt git [--head N] [FILE...]
       echo "task description" | prompt git [--head N] [FILE...]

Git workflow assistance prompt.
Helps with branching strategies, merge conflicts, rebasing, bisecting, and other git operations.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "You are a git expert. Help with the following git-related task."
echo
echo "Provide:"
echo "1. The exact git commands to run, in order."
echo "2. A brief explanation of what each command does and why."
echo "3. Common pitfalls or things that could go wrong at each step."
echo "4. If a task description is provided, implement exactly that."
echo
echo "Common scenarios you can help with:"
echo "- Merge conflict resolution strategies"
echo "- Interactive rebase workflows"
echo "- Bisecting to find problematic commits"
echo "- Setting up git hooks and workflows"
echo "- Recovering from mistakes (reset, reflog, cherry-pick)"
echo "- Repository maintenance (gc, prune, shallow clone management)"
echo "- Branch management and cleanup"
echo "- Cherry-picking and conflict resolution"
echo

# Embed git status context if in a git repo
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "Current git context:"
    echo
    echo '```text'
    echo "Branch: $(git branch --show-current 2>/dev/null || echo 'detached')"
    echo "Status:"
    git status --short 2>/dev/null | head -20
    echo
    echo "Recent commits:"
    git log --oneline -10 2>/dev/null
    echo '```'
    echo
fi

for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
        echo
    else
        echo "Warning: File '$file' not found." >&2
    fi
done
