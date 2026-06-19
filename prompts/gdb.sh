#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt gdb [--head N] [FILE...]
       some-command | prompt gdb [--head N] [FILE...]

Generates a prompt asking the LLM to write a GDB script that helps debug the specified code (e.g., tracking variables in loops, pretty-printing).

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Write a GDB script to help debug the core function or algorithm in the provided code."
echo "The GDB script should do the following:"
echo "1. Set breakpoints at key locations, such as function entry points, inside loops, and important conditional branches."
echo "2. Attach commands to these breakpoints to automatically print the relevant variables, loop counters, and memory states as the code executes."
echo "3. Ensure that complex data structures, arrays, and pointers are pretty-printed for readability."
echo "4. Include short comments in the GDB script explaining what each breakpoint and print command is tracking."
echo "Provide the final output as a single GDB script inside a code block."
echo "We want to run it with something like 'gdb -batch -q -x XXX.gdb ./build/test-XXX'"
echo "Don't clutter the output with too much information either; keep it clean."
echo "Try not to use python in the gdb scripts since most LLMs are not good at knowing gdb interface in python; do it if you're confident."
echo "Be mindful of errors like 'No symbol ... in current context.'"
echo

# Iterate through all collected file arguments
for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo
        echo '
```'
echo
else
echo "Warning: File '$file' not found." >&2
fi
done


