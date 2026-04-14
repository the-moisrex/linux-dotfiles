#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
files=()

show_help() {
  cat <<'EOF'
Usage: prompt review [--head N] [FILE...]
       some-command | prompt review [--head N] [FILE...]

Review this code like a strong practical reviewer.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}


source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Review this code like a strong practical reviewer."
echo "Prioritize bugs, behavioral regressions, risky assumptions, edge cases, maintainability problems, and missing tests."
echo "List the most important findings first with short explanations, then provide a git diff for the most useful fixes."
echo "If there are no major issues, say that clearly and still suggest a small safe improvement as a git diff if one is worthwhile."
echo

# Process all collected files
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
        echo "Warning: File '$file' not found." >&2
    fi
done
