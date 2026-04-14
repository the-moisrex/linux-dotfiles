#!/usr/bin/env bash
set -euo pipefail

file_path=""
stdin_piped=false
stdin_content=""
head_lines=""

show_help() {
    cat <<'EOF'
Usage: prompt symbols [--head N] [file]

Builds a prompt that reviews symbol names.
If a file path is given, it reads that file; otherwise it reads stdin.
Usage: `prompt symbols $(fzf)`

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
            file_path="$1"
            shift
            break
            ;;
    esac
done

if ! [ -t 0 ]; then
    stdin_piped=true
    stdin_content="$(cat)"
fi

if $stdin_piped && [[ -n "$stdin_content" ]]; then
    printf '%s

' "$stdin_content"
fi

file=$(basename "$file_path")

print_prompt() {
    if [[ -n "$file_path" ]]; then
        echo "Review the symbols in this file and find bad names that should be renamed."
        echo "Focus on unclear, misleading, overly abbreviated, inconsistent, or non-idiomatic symbol names."
        echo "For each rename suggestion, provide the current name, the proposed new name, and the reason in a table."
        echo "Only suggest renames that materially improve readability or maintainability."
        echo "At the end, provide a git diff that applies the renames in \`$file\`."
        echo
        echo "File: $file"
    else
        echo "Review the symbols in this file and find bad names that should be renamed."
        echo "Focus on unclear, misleading, overly abbreviated, inconsistent, or non-idiomatic symbol names."
        echo "For each rename suggestion, provide the current name, the proposed new name, and the reason in a table."
        echo "Only suggest renames that materially improve readability or maintainability."
        echo "At the end, provide a git diff that applies the renames."
    fi
    echo
    echo '```'
}

print_prompt

if [[ -n "$file_path" && -f "$file_path" ]]; then
    trim_context "$(cat -- "$file_path")"
elif $stdin_piped; then
    trim_context "$stdin_content"
fi

echo
echo '```'
