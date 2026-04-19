#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt bigo [--head N] [FILE...]
       some-command | prompt bigo [--head N] [FILE...]

Asks the AI to calculate the Big O time and space complexity of the provided algorithms.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Calculate the Big O notation for the time and space complexity of the following algorithms."
echo "Provide a clear step-by-step breakdown of your reasoning."
echo "Explicitly define what any variables (e.g., N, M, V, E) represent in the context of the inputs."
echo "If there are multiple functions or methods, analyze each one individually."
echo "ALWAYS format your Big O notations using math delimiters, such as \mathcal{O}(N) or \mathcal{O}(N \log N)."
echo

if [ $# -eq 0 ]; then
    # Fallback to fzf if no files were piped or provided as arguments
    set -- $(select_files)
fi

for file in "$@"; do
    if ! resolved_file="$(resolve_input_file "$file")"; then
        printf 'prompt bigo: file not found: %s\n' "$file" >&2
        continue
    fi
    
    if [[ ! -r "$resolved_file" ]]; then
        printf 'prompt bigo: file not readable: "%s"; resolved to "%s"\n' "$file" "$resolved_file" >&2
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

