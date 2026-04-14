#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""

show_help() {
  cat <<'EOF'
Usage: prompt stupid [FILE]
       some-command | prompt stupid [FILE]

Builds a prompt that asks for obvious mistakes, silly bugs, and easy-to-miss
issues in the provided code or text.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  show_help
  exit 0
fi

if ! [ -t 0 ]; then
  stdin_piped=true
  stdin_content="$(cat)"
fi

if $stdin_piped && [[ -n "$stdin_content" ]]; then
  printf '%s\n\n' "$stdin_content"
fi

echo "Find the stupid mistakes in this code."
echo "Focus on obvious bugs, wrong assumptions, copy-paste errors, bad edge cases, misleading names, missing checks, and anything else that would make an experienced reviewer say 'well that was silly'."
echo "Be blunt but useful. List each issue with a short explanation and the smallest practical fix."
echo

if [[ $# -gt 0 && -f "$1" ]]; then
  file_name="$(basename "$1")"
  echo "File: $file_name"
  echo
  echo '```'
  cat -- "$1"
elif $stdin_piped; then
  echo '```'
  printf '%s' "$stdin_content"
else
  echo '```'
fi

echo
echo '```'
