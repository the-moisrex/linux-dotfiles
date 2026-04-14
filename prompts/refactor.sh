#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
files=()

show_help() {
  cat <<'EOF'
Usage: prompt refactor [--head N] [FILE...]
       some-command | prompt refactor [--head N] [FILE...]

Refactor this while preserving behavior.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Refactor this while preserving behavior."
echo "Focus on clarity, structure, duplication removal, naming, cohesion, and simplifying control flow without changing the intended behavior."
echo "Briefly explain the refactor plan, then provide a git diff for the recommended changes."
echo "Keep the diff as small and safe as possible."
echo

# Process all collected files
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
        echo "Warning: File '$file' not found or is not a regular file." >&2
    fi
done
