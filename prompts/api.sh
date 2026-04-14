#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt api [--head N] [FILE]...
       some-command | prompt api [--head N] [FILE]...

Review this API design.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Review this API design."
echo "Look for confusing names, inconsistent behavior, unclear contracts, weak validation, awkward call sites, leaky abstractions, and backward-compatibility risks."
echo "Suggest the smallest meaningful API improvements, explain the tradeoffs briefly, and provide a git diff for the recommended changes."
echo

# Process ALL collected files
while [[ $# -gt 0 && -f "$1" ]]; do
    file_name="$(basename "$file")"
    echo "File: $file_name"
    echo
    echo "\`\`\`$(infer_lang "$file_name")"
    trim_context "$(cat -- "$file")"
    echo '```'
    echo
    shift
done
