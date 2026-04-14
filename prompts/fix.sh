#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""
files=() # Array to hold positional arguments (files)

show_help() {
  cat <<'EOF'
Usage: prompt fix [--head N] [FILE...]
       some-command | prompt fix [--head N] [FILE...]

Find the root problem here and propose the smallest useful fix.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

trim_context() {
    local content="$1"
    if [[ -n "$head_lines" ]]; then
        printf '%s' "$content" | head -n "$head_lines"
    else
        printf '%s' "$content"
    fi
}

# Parse all arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            show_help
            exit 0
        ;;
        --head)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --head" >&2
                exit 2
            fi
            head_lines="$2"
            shift 2
        ;;
        *)
            # Instead of breaking, collect the file arguments and continue parsing
            files+=("$1")
            shift
        ;;
    esac
done

if ! [ -t 0 ]; then
    stdin_piped=true
    stdin_content="$(cat)"
fi

if $stdin_piped && ! [ -v FROM_CLIPBOARD ] && [[ -n "$stdin_content" ]]; then
    printf '%s\n\n' "$stdin_content"
fi

echo "Find the root problem here and propose the smallest useful fix."
echo "Explain the issue briefly, mention any important assumptions, and provide the answer primarily as a git diff that can be applied directly."
echo "Prefer minimal, surgical changes over broad rewrites."
echo

# Iterate through all collected file arguments
for file in "${files[@]}"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo '```'
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
    else
        # Optional: warn if a passed file does not exist
        echo "Warning: File '$file' not found." >&2
    fi
done
