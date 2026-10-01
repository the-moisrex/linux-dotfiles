#!/usr/bin/env bash

show_help() {
    cat <<'EOF'
Usage: prompt list [--names]

List all available prompts with a short description.

The description is the first paragraph of the prompt's --help output,
joined into one line.

Options:
  --names        Print only prompt names, one per line (fast: skips --help)
  -h, --help     Show this help
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

# Tell the dispatcher not to copy this output to the clipboard.
if [[ -n "${PROMPT_NO_CLIPBOARD_FILE:-}" ]]; then
    printf 'no-clipboard\n' > "$PROMPT_NO_CLIPBOARD_FILE"
fi

names_only=false
for arg in "${ARGS[@]}"; do
    case "$arg" in
        --names) names_only=true ;;
        *)
            printf 'prompt list: unknown argument: %s\n' "$arg" >&2
            exit 2
            ;;
    esac
done

# First paragraph of help text: skip the Usage block, then join the
# non-indented prose lines until the first blank line.
first_paragraph() {
    awk '
        seen && /^$/ { exit }
        !seen && (/^Usage:/ || /^ +/ || /^$/) { next }
        { seen = 1; if ($0 ~ /^ +/) next; printf "%s ", $0 }
    ' | sed -E 's/ +$//'
}

short_help() {
    local file="$1" text
    if [[ "$file" == *.sh ]]; then
        text="$(bash "$file" --help </dev/null 2>/dev/null)"
    else
        text="$(cat -- "$file")"
    fi
    if [[ -z "$text" ]]; then
        text="(no help)"
    fi
    printf '%s\n' "$text" | first_paragraph | tr '\t' ' '
}

rows=""
while IFS=$'\t' read -r name file; do
    [[ -z "$name" ]] && continue
    if $names_only; then
        printf '%s\n' "$name"
        continue
    fi
    rows+="${name}"$'\t'"$(short_help "$file")"$'\n'
done < <(collect_prompts | sort)

if $names_only; then
    exit 0
fi

if [[ -n "${PROMPT_OUTPUT_TTY:-}" && -n "$rows" ]]; then
    printf '%-18s  %s\n' "Prompt" "Description"
    printf '%-18s  %s\n' "------" "-----------"
    while IFS=$'\t' read -r name desc; do
        [[ -z "$name" ]] && continue
        printf '%-18s  %s\n' "$name" "$desc"
    done <<< "$rows"
else
    printf '%s' "$rows"
fi
