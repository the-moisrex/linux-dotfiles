#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt optimize-prompt [--head N] [FILE...]
       echo "my rough prompt" | prompt optimize-prompt [--head N] [FILE...]

Ask the AI to analyze and improve an input prompt.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

cat <<'EOF'
Act as an expert prompt engineer. Review the provided prompt(s) or instructions below.
Your goal is to optimize the input to create a clearer, more effective, and more robust prompt for an AI assistant.

Provide:
1. A brief critique of the original prompt (identifying ambiguities, missing context, or structural issues).
2. A checklist of the specific improvements made.
3. The completely rewritten, optimized prompt enclosed in a markdown code block so it can be easily copied.

Here is the input to optimize:

EOF

# Iterate through all collected file arguments
for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo '```'
        echo
    else
        echo "Warning: File '$file' not found." >&2
    fi
done
