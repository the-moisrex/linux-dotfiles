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

