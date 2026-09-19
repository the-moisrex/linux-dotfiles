#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt verify [--head N] [GIT-DIRTY OPTIONS...]
       some-command | prompt verify [--head N] [GIT-DIRTY OPTIONS...]

Verify uncommitted changes in the current Git repository for correctness.
Embeds the full diff context (files + diffs) via git-dirty and asks the
AI to validate correctness, catch bugs, regressions, and missing edge cases.

All extra arguments are forwarded to git-dirty (e.g. --staged, --uncommitted,
--except, --diff, --full, --full-diff).  Default mode is --full-diff so the
AI sees both the current file state and the diff markers.

Options:
  --head N   Keep only the first N lines of each embedded context file
  -h, --help Show this help
EOF
}

NO_FILES=true
source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

script_dir="$(cd "$(dirname "$0")" && pwd)"

# Forward all remaining args to git-dirty; default to --full-diff
git_dirty_args=("--full-diff")
if [[ $# -gt 0 ]]; then
    git_dirty_args=("$@")
fi

dirty_output="$(bash "$script_dir/git-dirty.sh" "${git_dirty_args[@]}" 2>/dev/null || true)"

if [[ -z "$dirty_output" ]]; then
    echo "No changes found in the repository. Nothing to verify." >&2
    exit 0
fi

echo "You are verifying uncommitted changes in a Git repository."
echo "Your job is to validate correctness: check that the changes do"
echo "what they claim, handle edge cases, preserve invariants, and"
echo "introduce no bugs or regressions."
echo
echo "For each issue found:"
echo "  1. State whether the change is CORRECT or INCORRECT."
echo "  2. If incorrect, identify the file and line, explain the failure,"
echo "     and provide a git diff that fixes it."
echo
echo "If all changes are correct, state that clearly."
echo
trim_context "$dirty_output"
