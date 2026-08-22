#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt debug [--head N] [FILE...]
       some-command | prompt debug [--head N] [FILE...]

General-purpose multi-language debugging prompt.
Analyzes error messages, stack traces, or suspicious code and identifies the root cause.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "You are an expert debugger across all programming languages and runtimes."
echo "Analyze the provided code, error messages, stack traces, or logs and identify the root cause of the issue."
echo
echo "Approach:"
echo "1. Identify the language, runtime, and environment from the context."
echo "2. Trace the error from its symptom back to its origin."
echo "3. Explain the root cause clearly and concisely."
echo "4. Provide the smallest useful fix as a git diff."
echo "5. If the issue is ambiguous, list the most likely causes ranked by probability."
echo
echo "Focus on:"
echo "- Actual bugs, not style issues"
echo "- The first actionable fix rather than a list of everything wrong"
echo "- Edge cases that could cause intermittent failures"
echo "- Environmental issues (missing deps, wrong versions, path problems)"
echo "- Race conditions, resource leaks, and off-by-one errors"
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
        echo "Warning: File '$file' not found." >&2
    fi
done
