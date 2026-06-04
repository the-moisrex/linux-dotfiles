#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt commit [--head N]

Asks the AI to write a Git commit message based on current changes.
It prioritizes staged changes (git diff --cached). If no changes are staged,
it evaluates all unstaged changes (git diff).

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
NO_FILES=true
common_behavior
set -- "${ARGS[@]}"

# Ensure we are inside a git repository
if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "prompt commit: Error: Not inside a git repository." >&2
    exit 1
fi

# Get staged changes first
DIFF_CONTENT=$(git diff --cached)
DIFF_TYPE="staged changes"

# If no staged changes, fallback to all unstaged changes
if [ -z "$DIFF_CONTENT" ]; then
    DIFF_CONTENT=$(git diff)
    DIFF_TYPE="unstaged changes"
fi

# If still no changes, exit early
if [ -z "$DIFF_CONTENT" ]; then
    echo "prompt commit: No changes found in the repository." >&2
    exit 1
fi

echo "Write a clear, descriptive Git commit message for the following $DIFF_TYPE."
echo "Follow the Conventional Commits specification (e.g., feat:, fix:, docs:, refactor:, chore:)."
echo "Keep the subject line concise (under 50 characters) and include a more detailed body if the changes are complex."
echo "Provide the commit message directly, without extra conversational text."
echo
echo "Provide:"
echo "1. A concise subject line."
echo "2. A short body explaining what changed and why."
echo "3. If helpful, an alternative subject line."
echo "Keep it specific and practical, not generic."
echo
echo '
```diff'
trim_context "$DIFF_CONTENT"
echo '
```'
echo
