#!/usr/bin/env bash
set -euo pipefail

file_path="${1:-}"
file=$(basename "$file_path")
stdin_piped=false
stdin_content=""

show_help() {
    cat <<'EOF'
Usage: prompt symbols [file]

Builds a prompt that reviews symbol names.
If a file path is given, it reads that file; otherwise it reads stdin.
Usage: `prompt symbols $(fzf)`
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    show_help
    exit 0
fi

if ! [ -t 0 ]; then
    stdin_piped=true
    stdin_content="$(cat)"
fi

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

if $stdin_piped && [[ -n "$stdin_content" ]]; then
    printf '%s\n\n' "$stdin_content"
fi

print_prompt

if [[ -n "$file_path" && -f "$file_path" ]]; then
    cat -- "$file_path"
elif $stdin_piped; then
    printf '%s' "$stdin_content"
fi

echo
echo '```'
