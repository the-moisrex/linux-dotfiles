#!/bin/bash

curdir="$(realpath "$(dirname "$0")/../bin")"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "Usage: prompt spp <symbol-or-input>"
  echo
  echo "Builds a C++ debugging prompt and expands the given input through \`spp\`."
  exit 0
fi

print_prompt() {
  echo "I'm working on a C++ project and need help debugging an issue. Here's the relevant code:"
  echo
  echo '```cpp'
}

process_stdin() {
  # Read all stdin into a variable while preserving newlines
  local input
  input="$1"


  # Print the prompt
  print_prompt

  # Run spp and prompt symbol test_symbol on the input
  "$curdir/spp" "$input"
  echo
  echo '```'
  echo
  echo "What's wrong with this code and how can I fix it?"
}

process_stdin "$1"
