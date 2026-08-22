#!/usr/bin/env bash
set -euo pipefail


show_help() {
  cat <<'EOF'
Usage: prompt stupid [--head N] [file...]
       some-command | prompt stupid [--head N] [file...]

Find the stupid mistakes in this code.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}


source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Find the stupid mistakes in this code."
echo "Focus on obvious bugs, wrong assumptions, copy-paste errors, bad edge cases, misleading names, missing checks, and anything else that would make an experienced reviewer say 'well that was silly'."
echo "Be blunt but useful. List each issue with a short explanation and the smallest practical fix."
echo "At the end, suggest small git patches."
echo

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
