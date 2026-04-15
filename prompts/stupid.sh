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

while [[ $# -gt 0 && -f "$1" ]]; do
    file_name="$(basename "$1")"
    echo "File: $file_name"
    echo
    echo '```'
    trim_context "$(cat -- "$1")"
    echo
    echo '```'
    echo
    shift
done
