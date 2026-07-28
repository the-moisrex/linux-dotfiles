#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt metadata [--head N] [--format FORMAT] [FILE...]
       some-command | prompt metadata [--head N] [--format FORMAT] [FILE...]

Analyze the provided inputs and generate structured metadata representing them.

Options:
  --head N         Keep only the first N lines of the embedded context
  --format, -f     Output format for the metadata (e.g., json, xml, yaml, toml, conf). Default: json
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

format="json"

# Parse script-specific arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --format|-f)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --format" >&2
                exit 2
            fi
            format="$2"
            shift 2
        ;;
        *)
            shift
        ;;
    esac
done

echo "Analyze the input and generate comprehensive metadata representing its core attributes, entities, purpose, and context."
echo "Output the metadata strictly in ${format^^} format."
echo "Ensure the output is well-structured, valid ${format}, and enclosed in a single markdown code block so it can be easily extracted and parsed."
echo
