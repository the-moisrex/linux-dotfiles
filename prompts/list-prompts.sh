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

while IFS=$'\t' read -r name file; do
    [[ -z "$name" ]] && continue
    printf '=== %s ===\n' "$name"
    if [[ "$file" == *.sh ]]; then
        text="$(bash "$file" --help </dev/null 2>/dev/null)"
        if [[ -z "$text" ]]; then
            text="(no --help output)"
        fi
        printf '%s\n' "$text"
    else
        cat -- "$file"
    fi
    printf '\n'
done < <(collect_prompts | sort)
