#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
files=()

show_help() {
  cat <<'EOF'
Usage: prompt explain [--head N] [FILE]...
       some-command | prompt explain [--head N] [FILE]...

Explain this clearly and concretely.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Explain this clearly and concretely."
echo "Describe what it does, how the parts fit together, key control flow, important assumptions, and likely failure points."
echo "If useful, finish with a small git diff that improves readability through naming, structure, or comments."
echo

# Iterate over all collected files
for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo '```'
        echo
    else
        echo "Warning: File not found or is not a regular file - $file" >&2
    fi
done
