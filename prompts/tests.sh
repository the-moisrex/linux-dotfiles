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

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Review this code and identify the highest-value missing tests."
echo "Focus on edge cases, regressions, error handling, boundary conditions, invalid input, and behavior that looks easy to break."
echo "Propose a minimal but effective test plan first, then provide a git diff that adds or updates the tests."
echo "Prefer the smallest practical diff that materially improves confidence."
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
    else
        echo "Warning: File '$file' not found or is not a regular file." >&2
    fi
done