#!/usr/bin/env bash
set -euo pipefail

head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt files [--head N] [FILE...]
       some-command | prompt files [--head N] [FILE...]

Appends the given files as Markdown code blocks.
If you're inside a Git repository, file headings are printed relative to the
repository root.
If no files are provided, `fzf -m` is used to choose them interactively.

Options:
  --head N   Keep only the first N lines of each embedded file
EOF
}


source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


relative_path() {
    local file="$1"
    find_git_root
    if [[ -n "${GIT_ROOT:-}" ]]; then
        realpath --relative-to="$GIT_ROOT" "$file"
    else
        realpath --relative-to="$PWD" "$file"
    fi
}


# printf 'Additional file context:\n\n'

if [ $# -eq 0 ]; then
    set -- $(select_files)
fi

for file in "$@"; do
    if ! resolved_file="$(resolve_input_file "$file")"; then
        printf 'prompt files: file not found: %s\n' "$file" >&2
        continue
    fi
    
    if [[ ! -r "$resolved_file" ]]; then
        printf 'prompt files: file not readable: "%s"; resolved to "%s"\n' "$file" "$resolved_file" >&2
        continue
    fi
    
    rel_file="$(relative_path "$resolved_file")"
    lang="$(infer_lang "$resolved_file")"
    content="$(cat -- "$resolved_file")"
    
    if [ ! -z "$content" ]; then
        printf 'File %s\n\n' "$rel_file"
        printf '```%s\n' "$lang"
        trim_context "$content"
        printf '\n```\n\n'
    fi
done
