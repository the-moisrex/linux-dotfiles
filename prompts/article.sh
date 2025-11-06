#!/bin/bash

curdir="$(realpath "$(dirname "$0")/../bin")"

transform_url() {
  local url="$1"
  # Capture stdout for processing while letting stderr flow to stderr by using the command directly
  # without capturing stderr in the substitution
  printf '%s' "$(
    set -o pipefail
    "$curdir/yt.links" "$url" | xargs "$curdir/subtitle" | "$curdir/srt2text"
  )"
}

process_stdin() {
  # Read all stdin into a variable while preserving newlines
  local input
  input="$(cat -)"

  # Trim leading and trailing whitespace while preserving internal newlines using sed
  local trimmed
  trimmed="$(printf '%s' "$input" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"

  # URL regex: accept http(s)://... with at least one host char
  if printf '%s' "$trimmed" | grep -Eq '^https?://[^[:space:]]+$'; then
    # It's a URL: call transformer and print its output only if successful
    local result
    if result=$(transform_url "$trimmed"); then
      # Only print header and result if the command succeeds
      echo "Turn this into an article and remove the ads, repetition, meaningless stuff: "
      echo
      printf '%s' "$result"
    else
      # If command fails, transform_url already printed errors to stderr
      # Don't print anything to stdout, just exit with failure code
      exit 1
    fi
  else
    # Not a URL: print original stdin exactly with header
    echo "Turn this into an article and remove the ads, repetition, meaningless stuff: "
    echo
    printf '%s' "$input"
  fi
}

process_stdin
