#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
files=()

show_help() {
  cat <<'EOF'
Usage: prompt tests [--head N] [FILE...]
       some-command | prompt tests [--head N] [FILE...]

Review this code and identify the highest-value missing tests.

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
            files+=("$1")
            shift
        ;;
    esac
done

if ! [ -t 0 ]; then
    stdin_piped=true
    stdin_content="$(cat)"
fi

if $stdin_piped && ! [ -v FROM_CLIPBOARD ] && [[ -n "$stdin_content" ]]; then
    printf '%s\n\n' "$stdin_content"
fi

echo "Review this code and identify the highest-value missing tests."
echo "Focus on edge cases, regressions, error handling, boundary conditions, invalid input, and behavior that looks easy to break."
echo "Propose a minimal but effective test plan first, then provide a git diff that adds or updates the tests."
echo "Prefer the smallest practical diff that materially improves confidence."
echo

for file in "${files[@]}"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo '```'
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
    else
        echo "Warning: File '$file' not found or is not a regular file." >&2
    fi
done