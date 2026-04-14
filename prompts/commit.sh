#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
FILES=()

show_help() {
  cat <<'EOF'
Usage: prompt commit [--head N] [FILE...]
       some-command | prompt commit [--head N] [FILE...]

Write a strong git commit message for this change.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Write a strong git commit message for this change."
echo "Provide:"
echo "1. A concise subject line."
echo "2. A short body explaining what changed and why."
echo "3. If helpful, an alternative subject line."
echo "Keep it specific and practical, not generic."
echo

# Iterate through all gathered files
for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
        echo
    else
        echo "Warning: File '$file' not found." >&2
    fi
done
