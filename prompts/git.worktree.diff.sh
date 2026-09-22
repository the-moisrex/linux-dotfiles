#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt git.worktree.diff [--not <commit>] [--base <branch>] [--head N] [FILE...]

Shows Git diffs for files changed since the upstream branch.
By default, compares against the upstream tracking branch (or origin/main).
Explicit FILE arguments restrict the diff to only those files.

Options:
  --not <commit>   Exclude a commit (and its ancestors) from the diff
  --base <branch>  Base branch to compare against (default: upstream or origin/main)
  --head N         Keep only the first N lines of embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "prompt git.worktree.diff: Error: Not inside a git repository." >&2
    exit 1
fi

find_git_root

# --- Parse custom flags ---
base_branch=""
not_commit=""

set -- "${ARGS[@]}"
ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --not)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --not" >&2
                exit 2
            fi
            not_commit="$2"
            shift 2
            ;;
        --base)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --base" >&2
                exit 2
            fi
            base_branch="$2"
            shift 2
            ;;
        *)
            ARGS+=("$1")
            shift
            ;;
    esac
done

# --- Detect base branch ---
if [[ -z "$base_branch" ]]; then
    base_branch="$(git rev-parse --abbrev-ref @{u} 2>/dev/null || true)"
fi
if [[ -z "$base_branch" ]]; then
    base_branch="origin/main"
fi

# --- Build diff args ---
diff_args=("$base_branch")
if [[ -n "$not_commit" ]]; then
    diff_args+=(--not "$not_commit")
fi

# --- Show diff ---
if [[ ${#ARGS[@]} -gt 0 ]]; then
    diff_output="$(git diff "${diff_args[@]}" -- "${ARGS[@]}" 2>/dev/null || true)"
else
    diff_output="$(git diff "${diff_args[@]}" 2>/dev/null || true)"
fi

if [[ -n "$diff_output" ]]; then
    echo "Diffs (${base_branch}..HEAD):"
    echo
    echo '```diff'
    trim_context "$diff_output"
    echo '```'
else
    echo "No diffs found between current branch and ${base_branch}." >&2
fi
