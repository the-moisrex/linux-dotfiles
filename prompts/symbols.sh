#!/usr/bin/env bash
set -euo pipefail

file_path="${1:-}"

print_prompt() {
  if [[ -n "$file_path" ]]; then
    echo "Review the symbols in this file and find bad names that should be renamed."
    echo "Focus on unclear, misleading, overly abbreviated, inconsistent, or non-idiomatic symbol names."
    echo "For each rename suggestion, provide the current name, the proposed new name, and the reason."
    echo "Only suggest renames that materially improve readability or maintainability."
    echo "At the end, provide a git diff that applies the renames in \`$file_path\`."
    echo
    echo "File: $file_path"
  else
    echo "Review the symbols in this file and find bad names that should be renamed."
    echo "Focus on unclear, misleading, overly abbreviated, inconsistent, or non-idiomatic symbol names."
    echo "For each rename suggestion, provide the current name, the proposed new name, and the reason."
    echo "Only suggest renames that materially improve readability or maintainability."
    echo "At the end, provide a git diff that applies the renames."
  fi
  echo
  echo '```'
}

print_prompt

if [[ -n "$file_path" && -f "$file_path" ]]; then
  cat -- "$file_path"
else
  cat -
fi

echo
echo '```'
