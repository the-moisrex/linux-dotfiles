#!/bin/bash
set -euo pipefail

curdir="$(realpath "$(dirname "$0")/../bin")"
stdin_piped=false
stdin_content=""

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "Usage: prompt spp <symbol> [symbol...]"
  echo
  echo "Builds a C++ debugging prompt and expands the given symbols through \`spp\`."
  exit 0
fi

if ! [ -t 0 ]; then
  stdin_piped=true
  stdin_content="$(cat)"
fi

if [[ $# -eq 0 ]]; then
  echo "Usage: prompt spp <symbol> [symbol...]" >&2
  exit 2
fi

if $stdin_piped && [[ -n "$stdin_content" ]]; then
  printf '%s\n\n' "$stdin_content"
fi

echo "Additional C++ symbol context:"
echo

for symbol in "$@"; do
  echo "Symbol: $symbol"
  echo
  echo '```cpp'
  "$curdir/spp" "$symbol"
  echo
  echo '```'
  echo
done

echo "Use the symbol context above when analyzing the issue."
