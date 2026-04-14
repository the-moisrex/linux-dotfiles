#!/usr/bin/env bash
set -euo pipefail

files=()
stdin_piped=false
stdin_content=""
head_lines=""

show_help() {
    cat <<'EOF'
Usage: prompt symbols [--head N] [file...]

Builds a prompt that reviews symbol names.
If file paths are given, it reads those files; otherwise it reads stdin.
Usage: `prompt symbols $(fzf)`

Options:
  --head N   Keep only the first N lines of the embedded context
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

# Parse all arguments. Options are shifted away, files are kept.
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
            # Collect all non-flag arguments as files
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

print_prompt() {
    echo "Review the symbols in this code and find bad names that should be renamed."
    echo "Focus on unclear, misleading, overly abbreviated, inconsistent, or non-idiomatic symbol names."
    echo "For each rename suggestion, provide the current name, the proposed new name, and the reason in a table."
    echo "Only suggest renames that materially improve readability or maintainability."
    
    if [[ ${#files[@]} -gt 0 ]]; then
        echo "At the end, provide a git diff that applies the renames in the provided files."
    else
        echo "At the end, provide a git diff that applies the renames."
    fi
}

print_prompt

# Iterate through all the collected files
for file_path in "${files[@]}"; do
    if [[ -f "$file_path" ]]; then
        file=$(basename "$file_path")
        echo
        echo "File: $file"
        echo '```'
        trim_context "$(cat -- "$file_path")"
        echo
        echo '```'
    else
        echo "Warning: File not found or is not a regular file: $file_path" >&2
    fi
done
