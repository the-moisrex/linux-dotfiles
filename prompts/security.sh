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

trim_context() {
    local content="$1"
    if [[ -n "$head_lines" ]]; then
        printf '%s' "$content" | head -n "$head_lines"
    else
        printf '%s' "$content"
    fi
}

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
            # Collect any argument that isn't a known flag into the files array
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

echo "Review this for security and safety issues."
echo "Look for vulnerabilities, unsafe defaults, trust boundary mistakes, injection risks, missing validation, secrets exposure, privilege problems, dangerous file or process handling, and denial-of-service risks."
echo "Also call out reliability or safety hazards that could cause data loss, corruption, crashes, or harmful behavior."
echo "List findings by severity, explain the risk briefly, and suggest the smallest effective fix for each one."
echo

# Process all collected files
for file in "${files[@]}"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo '```'
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
        echo
    else
        echo "Warning: File '$file' not found or is not a regular file." >&2
    fi
done
