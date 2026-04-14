#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
files=() # Array to hold positional arguments (files)

show_help() {
  cat <<'EOF'
Usage: prompt fix [--head N] [FILE...]
       some-command | prompt fix [--head N] [FILE...]

Find the root problem here and propose the smallest useful fix.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}


source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Find the root problem here and propose the smallest useful fix."
echo "Explain the issue briefly, mention any important assumptions, and provide the answer primarily as a git diff that can be applied directly."
echo "Prefer minimal, surgical changes over broad rewrites."
echo

# Iterate through all collected file arguments
for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
    else
        # Optional: warn if a passed file does not exist
        echo "Warning: File '$file' not found." >&2
    fi
done
