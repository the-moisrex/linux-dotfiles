#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

show_help() {
  cat <<'EOF'
Usage: prompt url-spec [--head N] [QUERIES...]
       some-command | prompt url-spec [--head N] [QUERIES...]

Fetches relevant sections of the WHATWG URL specification using `whatwg-url-specs` and adds them to the prompt context.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Review the following sections from the WHATWG URL specification."
echo "Ensure that any code, fixes, or analysis strictly adhere to these standard algorithms and definitions."
echo

if [ $# -eq 0 ]; then
    echo "Warning: No queries provided. Use 'list' to see sections or provide specific keywords." >&2
    # Optionally, we can default to listing or showing help for the command
    echo '```text'
    $SCRIPT_DIR/../bin/whatwg-url-specs --help
    echo '```'
    echo
else
    echo "Querying WHATWG URL specification for: $*"
    echo
    echo '```markdown'
    # Run the whatwg-url-specs script with the given queries
    # We use trim_context in case the output needs to be truncated by --head
    trim_context "$($SCRIPT_DIR/../bin/whatwg-url-specs "$@")"
    echo '```'
    echo
fi
