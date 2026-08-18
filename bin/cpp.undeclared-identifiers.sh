#!/usr/bin/env bash
#
# cpp.undeclared-identifiers — suggest missing headers for undeclared symbols
# using clang-tidy diagnostics.
#
# Usage:
#   ./cpp.undeclared-identifiers [file.cpp] [clang compile flags...]
#
# If no file is provided, source is read from stdin.

set -euo pipefail

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    cat << EOF
Usage:
  $(basename "$0") [file.cpp] [clang compile flags...]

  Suggest missing headers for undeclared symbols using clang-tidy
  diagnostics. If no file is provided, source is read from stdin.

Examples:
  $(basename "$0") foo.cpp -Iinclude -std=c++20
  cat foo.cpp | $(basename "$0") -Iinclude
EOF
    exit 0
fi

TMPFILE=""
if [[ $# -gt 0 && -f "$1" ]]; then
  FILE="$1"
  shift
else
  TMPFILE="$(mktemp --suffix=.cpp)"
  cat > "$TMPFILE"
  FILE="$TMPFILE"
fi

FLAGS=("$@")

# Run clang-tidy with only the "undeclared identifier" diagnostic enabled
clang-tidy -checks=-clang-diagnostic-undeclared-identifier "$FILE" -- "${FLAGS[@]}" 2>&1 \
  | grep "\[clang-diagnostic-error\]" \
  | grep "use of undeclared identifier" \
  | sed -E "s/.*?'([^']+)'.*/\1/" \
  | sort -u

if [[ -n "$TMPFILE" ]]; then
  rm -f "$TMPFILE"
fi

