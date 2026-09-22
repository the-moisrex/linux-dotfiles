#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt git.files [--head N] [FILE...]
       some-command | prompt git.files [--head N] [FILE...]

Embeds the given files as Markdown code blocks.
File headings are printed relative to the Git repository root.
If no files are provided, `fzf -m` is used to choose from tracked files.

Options:
  --head N   Keep only the first N lines of each embedded file
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt
get_files || true


relative_path() {
    local file="$1"
    find_git_root
    if [[ -n "${GIT_ROOT:-}" ]]; then
        realpath --relative-to="$GIT_ROOT" "$file"
    else
        realpath --relative-to="$PWD" "$file"
    fi
}


if [ ${#ARGS[@]} -eq 0 ]; then
    # shellcheck disable=SC2046
    set -- $(select_files)
fi

for file in "${ARGS[@]}"; do
    if ! resolved_file="$(resolve_input_file "$file")"; then
        printf 'prompt git.files: file not found: %s\n' "$file" >&2
        continue
    fi

    if [[ ! -r "$resolved_file" ]]; then
        printf 'prompt git.files: file not readable: "%s"; resolved to "%s"\n' "$file" "$resolved_file" >&2
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
