#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt stupid [--head N] [file...]
       some-command | prompt stupid [--head N] [file...]

Find the stupid mistakes in this code.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

trim_context() {
    local content="$1"
    if [[ -n "$head_lines" ]]; then
        printf '%s' "$content" | head -n "$head_lines"
    else
        printf '%s' "$content"
    fi
}

# Array to hold all arguments that are not --head, --help, etc.
POSITIONAL_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            show_help
            exit 0
        ;;
        --head)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --head" >&2
                exit 2
            fi
            head_lines="$2"
            shift 2
        ;;
        *)
            # Save the argument and shift past it
            POSITIONAL_ARGS+=("$1")
            shift
        ;;
    esac
done

# Restore the positional arguments back to $@
set -- "${POSITIONAL_ARGS[@]}"

# Now $@ contains only the positional arguments, maintaining their original order.
# For example, you can now iterate over them or pass them to another command:
# echo "Remaining arguments: $@"


if ! [ -t 0 ]; then
    stdin_piped=true
    stdin_content="$(cat)"
fi

if $stdin_piped && ! [ -v FROM_CLIPBOARD ] && [[ -n "$stdin_content" ]]; then
    printf '%s

    ' "$stdin_content"
fi

echo "Find the stupid mistakes in this code."
echo "Focus on obvious bugs, wrong assumptions, copy-paste errors, bad edge cases, misleading names, missing checks, and anything else that would make an experienced reviewer say 'well that was silly'."
echo "Be blunt but useful. List each issue with a short explanation and the smallest practical fix."
echo

while [[ $# -gt 0 && -f "$1" ]]; do
    file_name="$(basename "$1")"
    echo "File: $file_name"
    echo
    echo '```'
    trim_context "$(cat -- "$1")"
    echo
    echo '```'
    echo
    shift
done
