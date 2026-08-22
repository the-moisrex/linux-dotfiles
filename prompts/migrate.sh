#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt migrate [--head N] [FILE...]
       echo "migration task" | prompt migrate [--head N] [FILE...]

Code migration assistance prompt.
Helps migrate code between frameworks, APIs, language versions, or patterns.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "You are a code migration expert."
echo "Help migrate the provided code from its current state to the target framework, API, or language version."
echo
echo "Approach:"
echo "1. Identify the current framework/API/version from the code."
echo "2. Identify the target from the task description or infer from context."
echo "3. List the breaking changes and deprecations that affect this code."
echo "4. Provide the migrated code as a git diff, preserving behavior."
echo "5. Note any manual steps required (config changes, dependency updates, data migrations)."
echo
echo "Guidelines:"
echo "- Preserve existing behavior unless explicitly asked to change it"
echo "- Make the smallest safe changes necessary"
echo "- Prefer mechanical transformations that can be verified"
echo "- Flag any semantic changes that cannot be done automatically"
echo "- Update imports, function signatures, and deprecated patterns"
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
