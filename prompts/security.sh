#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
files=()

show_help() {
  cat <<'EOF'
Usage: prompt security [--head N] [FILE...]
       some-command | prompt security [--head N] [FILE...]

Review this for security and safety issues.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


echo "Review this for security and safety issues."
echo "Look for vulnerabilities, unsafe defaults, trust boundary mistakes, injection risks, missing validation, secrets exposure, privilege problems, dangerous file or process handling, and denial-of-service risks."
echo "Also call out reliability or safety hazards that could cause data loss, corruption, crashes, or harmful behavior."
echo "List findings by severity, explain the risk briefly, and suggest the smallest effective fix for each one."
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
