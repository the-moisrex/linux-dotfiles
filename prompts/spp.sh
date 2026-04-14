#!/bin/bash
set -euo pipefail

curdir="$(realpath "$(dirname "$0")/../bin")"
stdin_piped=false
stdin_content=""
head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt spp [--head N] <symbol> [symbol...]

Builds a C++ debugging prompt and expands the given symbols through `spp`.

Options:
  --head N   Keep only the first N lines of each symbol expansion
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
      break
      ;;
  esac
done

if ! [ -t 0 ]; then
  stdin_piped=true
  stdin_content="$(cat)"
fi

if [[ $# -eq 0 ]]; then
  echo "Usage: prompt spp [--head N] <symbol> [symbol...]" >&2
  exit 2
fi

if $stdin_piped && [[ -n "$stdin_content" ]]; then
  printf '%s

' "$stdin_content"
fi

echo "Additional C++ symbol context:"
echo

for symbol in "$@"; do
  echo "Symbol: $symbol"
  echo
  echo '```cpp'
  trim_context "$("$curdir/spp" "$symbol")"
  echo
  echo '```'
  echo
done

echo "Use the symbol context above when analyzing the issue."
