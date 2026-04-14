#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
files=()

show_help() {
  cat <<'EOF'
Usage: prompt perf [--head N] [FILE...]
       some-command | prompt perf [--head N] [FILE...]

Review this for performance issues.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}


source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Review this for performance issues."
echo "Look for unnecessary allocations, wasteful copies, bad algorithms, blocking work, repeated parsing, excessive syscalls, poor data layout, and avoidable hot-path overhead."
echo "Call out which issues matter most in practice, and provide a git diff for the smallest high-impact improvements."
echo "Do not sacrifice correctness or readability for tiny wins."
echo

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
        echo "Warning: File not found or is not a regular file: $file" >&2
    fi
done
