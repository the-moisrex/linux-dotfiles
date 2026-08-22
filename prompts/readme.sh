#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt readme [--head N] [FILE...]
       echo "project description" | prompt readme [--head N] [FILE...]

Generates or improves a README.md for the provided code or project description.
If code files are provided, analyzes them to generate accurate documentation.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "You are a technical documentation expert."
echo "Generate or improve a comprehensive README.md based on the provided code and context."
echo
echo "The README should include:"
echo "1. Project name and a concise one-line description"
echo "2. Features and key capabilities"
echo "3. Installation instructions (copy-pasteable)"
echo "4. Quick start / usage examples"
echo "5. Configuration options (if any)"
echo "6. API reference (for libraries)"
echo "7. Contributing guidelines (if applicable)"
echo "8. License"
echo
echo "Guidelines:"
echo "- Write for someone who has never seen this project before"
echo "- Use concrete examples, not abstract descriptions"
echo "- Keep it scannable: headers, lists, code blocks"
echo "- Do not invent features that are not present in the code"
echo "- Match the tone to the project (professional for libraries, friendly for tools)"
echo

# Gather project context if in a git repo
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git_root="$(git rev-parse --show-toplevel)"
    echo "Project structure:"
    echo
    echo '```text'
    # Show top-level files and first-level directories
    ls -1 "$git_root" 2>/dev/null | head -30
    echo '```'
    echo
fi

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
