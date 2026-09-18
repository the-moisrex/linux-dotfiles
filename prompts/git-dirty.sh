#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt git-dirty [OPTIONS]

Show uncommitted/changed files in the current Git repository.

FILTER OPTIONS (what to show):
  --staged, -s           Only staged (cached) changes
  --uncommitted, -u      Only unstaged working tree changes
  --all, -a              Both staged and unstaged (default)
  --except               Invert the active filter
                         e.g. --staged --except = everything except staged
                              --all --except    = nothing (no filter)

OUTPUT OPTIONS (how to show):
  --files, -l            List filenames with status (default)
  --diff, -d             Show only the diffs
  --full, -f             Show full file contents
  --full-diff, -F        Show full files + diff context (@@ hunks with +/-)

OTHER:
  --head N               Keep only the first N lines of embedded context
  -h, --help             Show this help

DEFAULTS: --all --files

EXAMPLES:
  prompt git-dirty                          # All changes, file list
  prompt git-dirty --staged --diff          # Staged diffs only
  prompt git-dirty --uncommitted --full     # Full contents of unstaged files
  prompt git-dirty --staged --except        # Everything except staged
  prompt git-dirty --full-diff              # Full files with diff markers
EOF
}

source "$(dirname "$0")/_common.sh"
NO_FILES=true
head_lines=""
ARGS=("$@")

# --- Parse all flags (custom + --head/--help from _common.sh) ---
show_help_requested=false
filter_staged=false
filter_unstaged=false
filter_all=false
except=false
output_files=false
output_diff=false
output_full=false
output_full_diff=false

set -- "${ARGS[@]}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)          show_help_requested=true; shift ;;
        --head)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --head" >&2
                exit 2
            fi
            head_lines="$2"
            shift 2
            ;;
        --staged|-s)        filter_staged=true; shift ;;
        --uncommitted|-u)   filter_unstaged=true; shift ;;
        --all|-a)           filter_all=true; shift ;;
        --except)           except=true; shift ;;
        --files|-l)         output_files=true; shift ;;
        --diff|-d)          output_diff=true; shift ;;
        --full|-f)          output_full=true; shift ;;
        --full-diff|-F)     output_full_diff=true; shift ;;
        *)
            echo "prompt git-dirty: unknown option: $1" >&2
            echo "Try 'prompt git-dirty --help' for usage." >&2
            exit 1
            ;;
    esac
done

# Show help AFTER arg parsing so exit happens outside any pipeline subshell
if $show_help_requested; then
    show_help
    exit 0
fi

# --- Defaults ---
if ! $filter_staged && ! $filter_unstaged && ! $filter_all; then
    filter_all=true
fi

# --except with no explicit filter means --all first, then invert
if $except && ! $filter_staged && ! $filter_unstaged && ! $filter_all; then
    filter_all=true
fi

if ! $output_files && ! $output_diff && ! $output_full && ! $output_full_diff; then
    output_files=true
fi

# --- Validate git repo ---
if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "prompt git-dirty: Error: Not inside a git repository." >&2
    exit 1
fi

find_git_root

# --- Collect files and diffs ---
staged_files=""
staged_diff=""
unstaged_files=""
unstaged_diff=""
untracked_files=""

if $filter_staged; then
    staged_files="$(git diff --cached --name-status 2>/dev/null || true)"
    staged_diff="$(git diff --cached 2>/dev/null || true)"
fi

if $filter_unstaged; then
    unstaged_files="$(git diff --name-status 2>/dev/null || true)"
    unstaged_diff="$(git diff 2>/dev/null || true)"
    untracked_files="$(git ls-files --others --exclude-standard 2>/dev/null || true)"
fi

if $filter_all; then
    staged_files="$(git diff --cached --name-status 2>/dev/null || true)"
    staged_diff="$(git diff --cached 2>/dev/null || true)"
    unstaged_files="$(git diff --name-status 2>/dev/null || true)"
    unstaged_diff="$(git diff 2>/dev/null || true)"
    untracked_files="$(git ls-files --others --exclude-standard 2>/dev/null || true)"
fi

# --- Apply --except inversion ---
if $except; then
    if $filter_staged; then
        # Show everything EXCEPT staged
        unstaged_files="$(git diff --name-status 2>/dev/null || true)"
        unstaged_diff="$(git diff 2>/dev/null || true)"
        untracked_files="$(git ls-files --others --exclude-standard 2>/dev/null || true)"
        staged_files=""
        staged_diff=""
    elif $filter_unstaged; then
        # Show everything EXCEPT unstaged
        staged_files="$(git diff --cached --name-status 2>/dev/null || true)"
        staged_diff="$(git diff --cached 2>/dev/null || true)"
        unstaged_files=""
        unstaged_diff=""
        untracked_files=""
    elif $filter_all; then
        # --all --except = show nothing
        staged_files=""
        staged_diff=""
        unstaged_files=""
        unstaged_diff=""
        untracked_files=""
    fi
fi

# --- Check if there's anything to show ---
has_content=false
[[ -n "$staged_files" ]] && has_content=true
[[ -n "$unstaged_files" ]] && has_content=true
[[ -n "$untracked_files" ]] && has_content=true

if ! $has_content; then
    echo "No changes found in the repository." >&2
    exit 0
fi

# --- AI instructions ---
echo "You are reviewing changes in a Git repository."
echo "Analyze the following file changes and provide insights, suggestions, or summaries as appropriate."
echo

# --- Render output ---
render_file_list() {
    local label="$1" files="$2"
    if [[ -n "$files" ]]; then
        echo "$label"
        echo '```text'
        echo "$files"
        echo '```'
        echo
    fi
}

render_diff() {
    local label="$1" diff_content="$2"
    if [[ -n "$diff_content" ]]; then
        echo "$label"
        echo
        echo '```diff'
        trim_context "$diff_content"
        echo '```'
        echo
    fi
}

render_full_files() {
    local label="$1" files="$2"
    if [[ -z "$files" ]]; then
        return
    fi
    echo "$label"
    echo
    while IFS=$'\t' read -r status filepath; do
        if [[ -f "$GIT_ROOT/$filepath" ]]; then
            local name
            name="$(basename "$filepath")"
            echo "File: $filepath (status: $status)"
            echo
            echo "\`\`\`$(infer_lang "$name")"
            trim_context "$(cat -- "$GIT_ROOT/$filepath")"
            echo '```'
            echo
        fi
    done <<< "$files"
}

render_full_diff() {
    local label="$1" files="$2" diff_content="$3"
    if [[ -z "$files" ]]; then
        return
    fi
    echo "$label"
    echo
    while IFS=$'\t' read -r status filepath; do
        if [[ -f "$GIT_ROOT/$filepath" ]]; then
            local name
            name="$(basename "$filepath")"
            echo "File: $filepath (status: $status)"
            echo
            echo "\`\`\`$(infer_lang "$name")"
            trim_context "$(cat -- "$GIT_ROOT/$filepath")"
            echo '```'
            echo
        fi
    done <<< "$files"

    if [[ -n "$diff_content" ]]; then
        echo "Diffs:"
        echo
        echo '```diff'
        trim_context "$diff_content"
        echo '```'
        echo
    fi
}

render_untracked() {
    local label="$1" files="$2"
    if [[ -z "$files" ]]; then
        return
    fi
    echo "$label"
    echo
    while IFS= read -r filepath; do
        if [[ -f "$GIT_ROOT/$filepath" ]]; then
            local name
            name="$(basename "$filepath")"
            echo "File: $filepath (new)"
            echo
            echo "\`\`\`$(infer_lang "$name")"
            trim_context "$(cat -- "$GIT_ROOT/$filepath")"
            echo '```'
            echo
        fi
    done <<< "$files"
}

if $output_files; then
    if [[ -n "$staged_files" ]]; then
        render_file_list "Staged changes:" "$staged_files"
    fi
    if [[ -n "$unstaged_files" ]]; then
        render_file_list "Unstaged changes:" "$unstaged_files"
    fi
    if [[ -n "$untracked_files" ]]; then
        render_file_list "Untracked files:" "$untracked_files"
    fi

elif $output_diff; then
    if [[ -n "$staged_diff" ]]; then
        render_diff "Staged diff:" "$staged_diff"
    fi
    if [[ -n "$unstaged_diff" ]]; then
        render_diff "Unstaged diff:" "$unstaged_diff"
    fi
    render_untracked "Untracked files:" "$untracked_files"

elif $output_full; then
    if [[ -n "$staged_files" ]]; then
        render_full_files "Staged files:" "$staged_files"
    fi
    if [[ -n "$unstaged_files" ]]; then
        render_full_files "Unstaged files:" "$unstaged_files"
    fi
    render_untracked "Untracked files:" "$untracked_files"

elif $output_full_diff; then
    if [[ -n "$staged_files" ]]; then
        render_full_diff "Staged files:" "$staged_files" "$staged_diff"
    fi
    if [[ -n "$unstaged_files" ]]; then
        render_full_diff "Unstaged files:" "$unstaged_files" "$unstaged_diff"
    fi
    render_untracked "Untracked files:" "$untracked_files"
fi
