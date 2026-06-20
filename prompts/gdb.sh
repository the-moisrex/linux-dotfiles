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

echo "Write a short GDB script to help debug the core function or algorithm in the provided code."
echo "IMPORTANT: Do NOT provide a generic or generalized GDB template. You must specifically target the code provided below, using the actual function names, variable names, and logic present in these exact snippets."
echo
echo "To ensure the script actually works and doesn't fail due to minor line number mismatches or hanging, use the following techniques:"
echo "1. **Breakpoints**: Avoid guessing exact line numbers. Prefer function names (\`tbreak actual_func_name\`), relative line numbers (\`tbreak +5\`), or \`rbreak\`."
echo "2. **Tracing Execution**: Instead of standard breakpoints that halt execution, highly prefer \`dprintf\` for tracing loops and states (e.g., \`dprintf actual_func_name, \"actual_var=%d\\n\", actual_var\`). If using standard breakpoints, remember to add a \`commands\` block ending with \`continue\`."
echo "3. **Loop Debugging**: Use conditional breakpoints (\`break my_loop if i == 50\`) or the \`ignore <bnum> <count>\` command to skip the noise of early loop iterations."
echo "4. **Loop Debugging**: Use tbreaks and other logging breaks or conditional breaks to log and print anomolies or verifiy the loops outputs."
echo "5. **Data Inspection**: Use GDB's array slice syntax to print dynamically allocated arrays (e.g., \`p *actual_array@10\`)."
echo
echo "Be mindful of errors like 'No symbol ... in current context.' Try to break at locations where the variables are definitely in scope."
echo "Avoid using the Python GDB API unless you are absolutely confident; stick to native GDB commands."
echo
cat <<'EOF'
**Important GDB reliability requirement:**
Do not use `commands` immediately after `rbreak` unless you first verify that `rbreak` actually created at least one breakpoint. `rbreak` can print `No breakpoints made` for templated C++ functions if no matching instantiation exists in the current binary/debug symbols, and then a following `commands` block fails with `Argument required`.

For templated functions either:

 1. use `set breakpoint pending on` with a concrete mangled/demangled instantiated symbol if known, or  
 2. use `break file:function` only if supported, or  
 3. use `rbreak ...` only as a standalone optional command and do **not** attach a `commands` block to it, or  
 4. break on a nearby caller or on a non-template helper function that is actually emitted, then step into the templated function.

The script must be robust when the breakpoint pattern matches nothing. It should not contain a `commands` block that depends on a possibly nonexistent `rbreak` result.

Avoid `rbreak ...` followed by `commands` for templated functions. If `rbreak` finds no instantiated symbol, the script will fail with `Argument required`. Use a concrete existing symbol or make the `rbreak` optional and do not attach commands unless a breakpoint number is guaranteed.

We probably run it using `gdb -batch` as well.
EOF

echo
echo
echo 'Running the command: `gdb -q --nh -batch -ex "help" -ex "help data" -ex "help breakpoints" -ex "help tracepoint" | grep -Ev "^(Type|Command name|Making program|set )"``'
gdb -q --nh -batch -ex "help" -ex "help data" -ex "help breakpoints" -ex "help tracepoint" | grep -Ev "^(Type|Command name|Making program|set )"


# Iterate through all collected file arguments
for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo '
```'
echo
else
echo "Warning: File '$file' not found." >&2
fi
done


