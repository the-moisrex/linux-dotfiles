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

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Review the symbols in this code and find bad names that should be renamed."
echo "Focus on unclear, misleading, overly abbreviated, inconsistent, or non-idiomatic symbol names."
echo "For each rename suggestion, provide the current name, the proposed new name, and the reason in a table."
echo "Only suggest renames that materially improve readability or maintainability."

if [[ $# -gt 0 ]]; then
    echo "At the end, provide a git diff that applies the renames in the provided files."
else
    echo "At the end, provide a git diff that applies the renames."
fi


# Iterate through all the collected files
for file_path in "$@"; do
    if [[ -f "$file_path" ]]; then
        file=$(basename "$file_path")
        echo
        echo "File: $file"
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file_path")"
        echo
        echo '```'
    else
        echo "Warning: File not found or is not a regular file: $file_path" >&2
    fi
done
