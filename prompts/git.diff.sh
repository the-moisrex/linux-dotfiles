#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt git.diff [--head N] [FILE...]

Shows Git diffs for the specified files.
If no files are provided, shows all unstaged changes and untracked files.

Options:
  --head N   Keep only the first N lines of embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "prompt git.diff: Error: Not inside a git repository." >&2
    exit 1
fi

find_git_root

if [[ ${#ARGS[@]} -gt 0 ]]; then
    diff_output="$(git diff -- "${ARGS[@]}" 2>/dev/null || true)"
    if [[ -n "$diff_output" ]]; then
        echo "Diffs:"
        echo
        echo '```diff'
        trim_context "$diff_output"
        echo '```'
    else
        echo "No diffs found for the specified files." >&2
    fi
else
    unstaged_diff="$(git diff 2>/dev/null || true)"
    untracked_files="$(git ls-files --others --exclude-standard 2>/dev/null || true)"

    if [[ -n "$unstaged_diff" ]]; then
        echo "Unstaged changes:"
        echo
        echo '```diff'
        trim_context "$unstaged_diff"
        echo '```'
        echo
    fi

    if [[ -n "$untracked_files" ]]; then
        echo "Untracked files:"
        echo
        while IFS= read -r filepath; do
            if [[ -f "$GIT_ROOT/$filepath" ]]; then
                name="$(basename "$filepath")"
                echo "File: $filepath (new)"
                echo
                echo "\`\`\`$(infer_lang "$name")"
                trim_context "$(cat -- "$GIT_ROOT/$filepath")"
                echo '```'
                echo
            fi
        done <<< "$untracked_files"
    fi

    if [[ -z "$unstaged_diff" && -z "$untracked_files" ]]; then
        echo "No changes found in the working tree." >&2
    fi
fi
