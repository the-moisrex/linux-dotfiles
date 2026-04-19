#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt docstring [--head N] [FILE...]
       some-command | prompt docstring [--head N] [FILE...]

Asks the AI to add or improve docstrings for functions, classes, and methods in the provided code.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Add comprehensive docstrings to all functions, classes, and methods in the following code."
echo "Ensure the docstrings clearly describe:"
echo "1. The overall purpose and behavior."
echo "2. The parameters (including their expected types)."
echo "3. The return values (and types)."
echo "4. Any exceptions or errors that might be raised."
echo "5. Don't explain trivial or add useless information."
echo "Adhere to standard documentation conventions for the specific language if they exist (e.g., PEP 257/Google style for Python, JSDoc for JavaScript/TypeScript, JavaDoc for Java)."
echo "Return the updated code with the newly added docstrings or write a patch file."
echo

if [ $# -eq 0 ]; then
    # Fallback to fzf if no files were piped or provided as arguments
    set -- $(select_files)
fi

for file in "$@"; do
    if ! resolved_file="$(resolve_input_file "$file")"; then
        printf 'prompt docstring: file not found: %s\n' "$file" >&2
        continue
    fi
    
    if [[ ! -r "$resolved_file" ]]; then
        printf 'prompt docstring: file not readable: "%s"; resolved to "%s"\n' "$file" "$resolved_file" >&2
        continue
    fi
    
    file_name="$(basename "$resolved_file")"
    echo "File: $file_name"
    echo
    echo "\`\`\`$(infer_lang "$resolved_file")"
    trim_context "$(cat -- "$resolved_file")"
    printf '\n%s' '```'
    echo
done

