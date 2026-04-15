#!/usr/bin/env bash
set -euo pipefail

prepend=false

show_help() {
  cat <<'EOF'
Usage: prompt note [--head N] [NOTE...]
       some-command | prompt stupid [--head N] [NOTE...]

Add note to the prompt

Options:
  --head N      Keep only the first N lines of the embedded context
  --prepend|-p  Add before everything
EOF
}

parse_input_arguments() {
    set -- "${ARGS[@]}"
    ARGS=()
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
            --prepend|-p)
                prepend=true
                shift
            ;;
            *)
                ARGS+=("$1")
                shift
            ;;
        esac
    done
}

print_note() {
    if ! $prepend; then
        echo
    fi
    while [[ $# -gt 0 ]]; do
        echo "$1"
        echo
        shift
    done
}

source "$(dirname "$0")/_common.sh"
parse_input_arguments
set -- "${ARGS[@]}"

if $prepend; then
    print_note "$@"
    print_stdin
else
    print_stdin
    print_note "$@"
fi
