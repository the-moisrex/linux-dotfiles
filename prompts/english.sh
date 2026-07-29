#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt english [--head N]...
       some-command | prompt english [--head N]...

Translate to English.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
parse_arguments
set -- "${ARGS[@]}"

echo "Translate to English."
echo
cat
