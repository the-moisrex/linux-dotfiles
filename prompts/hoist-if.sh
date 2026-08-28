#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt hoist-if [--head N] [FILE...]
       some-command | prompt hoist-if [--head N] [FILE...]

Hoist invariant if statements out of loops for performance.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Analyze the code below and find opportunities to move if statements outside of for/while loops."
echo "Focus on conditions that are loop-invariant: the condition's result does not change across iterations."
echo "For each opportunity:"
echo "  1. Explain why the condition is invariant (what makes it safe to hoist)."
echo "  2. Provide a git diff showing the refactored code."
echo "  3. Note any caveats (readability trade-offs, cases where hoisting is invalid)."
echo "Do not hoist conditions that depend on the loop variable or mutate state inside the loop."
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
        echo "Warning: File '$file' not found or is not a regular file." >&2
    fi
done
