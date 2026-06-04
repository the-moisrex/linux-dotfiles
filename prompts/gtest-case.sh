#!/bin/bash
set -euo pipefail

curdir="$(realpath "$(dirname "$0")/../bin")"
stdin_piped=false
stdin_content=""
head_lines=""
exact_args=()

show_help() {
  cat <<'EOF'
Usage: prompt gtest-case [--head N] [--exact] <test-name> [test-name...]

Builds a debugging prompt and embeds the original source for Google Test cases.
By default, test names are prefix matches.

Options:
  --head N   Keep only the first N lines of each test case
  --exact    Match each given test name exactly
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
        --exact)
            exact_args+=(--exact)
            shift
        ;;
        *)
            POSITIONAL_ARGS+=("$1")
            shift
        ;;
    esac
done

set -- "${POSITIONAL_ARGS[@]}"

if ! [ -t 0 ]; then
    stdin_piped=true
    stdin_content="$(cat)"
fi

if [[ $# -eq 0 ]]; then
    echo "Usage: prompt gtest-case [--head N] [--exact] <test-name> [test-name...]" >&2
    exit 2
fi

if $stdin_piped && ! [ -v FROM_CLIPBOARD ] && [[ -n "$stdin_content" ]]; then
    printf '%s\n\n' "$stdin_content"
fi

echo "Additional Google Test case context:"
echo

for test_name in "$@"; do
    echo "Test: $test_name"
    echo
    echo '```cpp'
    trim_context "$("$curdir/gtest-case" "${exact_args[@]}" "$test_name")"
    echo
    echo '```'
    echo
done

echo "Use the test case context above when analyzing the issue."
