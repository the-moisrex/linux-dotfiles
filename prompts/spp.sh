#!/bin/bash
set -euo pipefail

curdir="$(realpath "$(dirname "$0")/../bin")"
stdin_piped=false
stdin_content=""
head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt spp [--head N] <symbol> [symbol...]

Builds a C++ debugging prompt and expands the given symbols through `spp`.

Options:
  --head N   Keep only the first N lines of each symbol expansion
EOF
}

trim_context() {
    local content="$1"
    if [[ -n "$head_lines" ]]; then
        printf '%s\n' "$content" | head -n "$head_lines"
    else
        printf '%s\n' "$content"
    fi
}

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
            # Save it in an array for later and shift past it
            POSITIONAL_ARGS+=("$1")
            shift
        ;;
    esac
done

# Restore positional arguments so `$@` and `$#` work as expected below
set -- "${POSITIONAL_ARGS[@]}"

if ! [ -t 0 ]; then
    stdin_piped=true
    stdin_content="$(cat)"
fi

if [[ $# -eq 0 ]]; then
    echo "Usage: prompt spp [--head N] <symbol> [symbol...]" >&2
    exit 2
fi

if $stdin_piped && ! [ -v FROM_CLIPBOARD ] && [[ -n "$stdin_content" ]]; then
    printf '%s\n\n' "$stdin_content"
fi

echo "Additional C++ symbol context:"
echo

# Now iterates over all accumulated positional arguments properly
for symbol in "$@"; do
    echo "Symbol: $symbol"
    echo
    echo '```cpp'
    trim_context "$("$curdir/spp" "$symbol")"
    echo
    echo '```'
    echo
done

echo "Use the symbol context above when analyzing the issue."
