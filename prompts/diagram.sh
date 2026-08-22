#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt diagram [--head N] [--type TYPE] [FILE...]
       echo "task description" | prompt diagram [--head N] [--type TYPE] [FILE...]

Generates code diagrams (Mermaid, PlantUML, ASCII) from source code or descriptions.

Options:
  --head N       Keep only the first N lines of the embedded context
  --type, -t     Diagram type: mermaid (default), plantuml, ascii, flowchart, sequence, class
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

diagram_type="mermaid"

# Parse script-specific arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --type|-t)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --type" >&2
                exit 2
            fi
            diagram_type="$2"
            shift 2
        ;;
        *)
            shift
        ;;
    esac
done

echo "You are a software architecture diagramming expert."
echo "Generate a clear, accurate diagram from the provided code or description."
echo
echo "Output format: ${diagram_type}"
echo
echo "Guidelines:"
echo "- Capture the essential relationships, not every detail"
echo "- Use meaningful labels on all nodes and edges"
echo "- Group related components together"
echo "- For sequence diagrams, show the most important interactions"
echo "- For class diagrams, show inheritance, composition, and key methods"
echo "- For flowcharts, show decision points and error paths"
echo "- Include a brief legend or title if the diagram is complex"
echo
echo "If the code is complex, produce multiple focused diagrams rather than one overwhelming diagram."
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
