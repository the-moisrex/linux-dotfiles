#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt git.worktree.files [--not <commit>] [--base <branch>] [--head N] [FILE...]

Embeds files changed since the upstream branch as Markdown code blocks.
By default, compares against the upstream tracking branch (or origin/main).
Explicit FILE arguments restrict embedding to only those changed files.

Options:
  --not <commit>   Exclude a commit (and its ancestors) from the diff
  --base <branch>  Base branch to compare against (default: upstream or origin/main)
  --head N         Keep only the first N lines of each embedded file
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "prompt git.worktree.files: Error: Not inside a git repository." >&2
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

# --- Get changed files ---
diff_args=("$base_branch")
if [[ -n "$not_commit" ]]; then
    diff_args+=(--not "$not_commit")
fi

if [[ ${#ARGS[@]} -gt 0 ]]; then
    changed_files=("${ARGS[@]}")
else
    changed_files=($(git diff "${diff_args[@]}" --name-only 2>/dev/null))
fi

if [[ ${#changed_files[@]} -eq 0 ]]; then
    echo "No changed files found between current branch and ${base_branch}." >&2
    exit 0
fi

# --- Embed files ---
relative_path() {
    local file="$1"
    if [[ -n "${GIT_ROOT:-}" ]]; then
        realpath --relative-to="$GIT_ROOT" "$file"
    else
        realpath --relative-to="$PWD" "$file"
    fi
}

for file in "${changed_files[@]}"; do
    if ! resolved_file="$(resolve_input_file "$file")"; then
        printf 'prompt git.worktree.files: file not found: %s\n' "$file" >&2
        continue
    fi

    if [[ ! -r "$resolved_file" ]]; then
        printf 'prompt git.worktree.files: file not readable: "%s"; resolved to "%s"\n' "$file" "$resolved_file" >&2
        continue
    fi

    rel_file="$(relative_path "$resolved_file")"
    lang="$(infer_lang "$resolved_file")"
    content="$(cat -- "$resolved_file")"

    if [ -n "$content" ]; then
        if [[ -n "$head_lines" ]]; then
            printf 'File %s (first %s lines)\n\n' "$rel_file" "$head_lines"
        else
            printf 'File %s\n\n' "$rel_file"
        fi
        printf '```%s\n' "$lang"
        trim_context "$content"
        printf '\n```\n\n'
    fi
done
