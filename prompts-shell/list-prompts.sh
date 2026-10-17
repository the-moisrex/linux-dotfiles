#!/usr/bin/env bash

show_help() {
    cat <<'EOF'
Usage: prompt list-prompts

Print the full help text of every available prompt, one after another.
Bash prompts are shown via their --help output; .txt/.md prompts are
printed in full.

See also: prompt list (short descriptions, one line per prompt).
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

# Tell the dispatcher not to copy this output to the clipboard.
if [[ -n "${PROMPT_NO_CLIPBOARD_FILE:-}" ]]; then
    printf 'no-clipboard\n' > "$PROMPT_NO_CLIPBOARD_FILE"
fi

if [[ ${#ARGS[@]} -gt 0 ]]; then
    printf 'prompt list-prompts: takes no arguments\n' >&2
    exit 2
fi

pairs=()
sh_files=()
while IFS=$'\t' read -r name file; do
    [[ -z "$name" ]] && continue
    pairs+=("${name}"$'\t'"${file}")
    [[ "$file" == *.sh ]] && sh_files+=("$file")
done < <(collect_prompts | sort)

# One awk run extracts every .sh prompt's full help text; prompts without
# a static show_help heredoc fall back to running --help.
declare -A static_help=()
if ((${#sh_files[@]})); then
    cur_file=""
    cur_body=""
    while IFS= read -r line; do
        if [[ "$line" == $'\x1c'* ]]; then
            [[ -n "$cur_file" ]] && static_help["$cur_file"]="$cur_body"
            cur_file="${line#$'\x1c'}"
            cur_body=""
        else
            cur_body+="$line"$'\n'
        fi
    done < <(extract_help full "${sh_files[@]}")
    [[ -n "$cur_file" ]] && static_help["$cur_file"]="$cur_body"
fi

for pair in "${pairs[@]}"; do
    name="${pair%%$'\t'*}"
    file="${pair#*$'\t'}"
    printf '=== %s ===\n' "$name"
    if [[ "$file" == *.sh ]]; then
        if [[ -n "${static_help[$file]:-}" ]]; then
            printf '%s' "${static_help[$file]}"
        else
            text="$(bash "$file" --help </dev/null 2>/dev/null)"
            if [[ -z "$text" ]]; then
                text="(no --help output)"
            fi
            printf '%s\n' "$text"
        fi
    else
        cat -- "$file"
    fi
    printf '\n'
done
