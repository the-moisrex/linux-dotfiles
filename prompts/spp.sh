#!/bin/bash

curdir="$(realpath "$(dirname "$0")/../bin")"

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
