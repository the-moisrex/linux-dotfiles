#!/usr/bin/env bash
set -euo pipefail

curdir="$(realpath "$(dirname "$0")/../bin")"

show_help() {
  cat <<'EOF'
Usage: prompt spp [--head N] <symbol> [symbol...]

Builds a C++ debugging prompt and expands the given symbols through `spp`.

Options:
  --head N   Keep only the first N lines of each symbol expansion
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

if [[ $# -eq 0 ]]; then
    echo "Usage: prompt spp [--head N] <symbol> [symbol...]" >&2
    exit 2
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
