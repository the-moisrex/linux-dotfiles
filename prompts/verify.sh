#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt verify [--head N] [GIT-DIRTY OPTIONS...]
       some-command | prompt verify [--head N] [GIT-DIRTY OPTIONS...]

Verify uncommitted changes in the current Git repository for correctness,
or verify whatever is piped in on stdin (e.g. from a clipboard, a saved
diff, or the output of `git show`).

When stdin is piped, that content is used as the context instead of
git-dirty. Otherwise the full diff context (files + diffs) is embedded
via git-dirty and the AI is asked to validate correctness, catch bugs,
regressions, and missing edge cases.

All extra arguments are forwarded to git-dirty (e.g. --staged,
--uncommitted, --except, --diff, --full, --full-diff).  Default mode is
--full-diff so the AI sees both the current file state and the diff
markers.

Options:
  --head N   Keep only the first N lines of each embedded context file
  -h, --help Show this help
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

script_dir="$(cd "$(dirname "$0")" && pwd)"

# Decide what to verify. print_stdin() consumes piped stdin even with
# NO_FILES=true, and records the result via STDIN_CONSUMED/stdin_content.
context=""
if [[ "${STDIN_CONSUMED:-false}" == "true" ]]; then
    context="$stdin_content"
else
    git_dirty_args=("--full-diff")
    if [[ $# -gt 0 ]]; then
        git_dirty_args=("${ARGS[@]}")
    fi
    context="$(bash "$script_dir/git-dirty.sh" "${git_dirty_args[@]}" 2>/dev/null || true)"
    if [[ -z "$context" ]]; then
        echo "No changes found in the repository. Nothing to verify." >&2
        exit 0
    fi
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
trim_context "$context"
