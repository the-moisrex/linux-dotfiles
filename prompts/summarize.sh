#!/usr/bin/env bash
set -o nounset
# don't enable -e globally because we use explicit if/else checks;
# keep pipefail local where needed (we also set it inside transform_url)

curdir="$(realpath "$(dirname "$0")/../bin")"

print_prompt() {
  echo "Summarize this and remove the ads, repetition, meaningless stuff: "
  echo
}

transform_url() {
  local url="$1"

  # make sure any failure in the pipeline is visible
  set -o pipefail

  # run pipeline inside a command substitution but check its exit status
  local out
  if ! out="$("$curdir/yt.links" "$url" | xargs "$curdir/subtitle" | "$curdir/srt2text")"; then
    # pipeline failed — propagate failure and don't print anything to stdout
    return 1
  fi

  # success: print captured output
  printf '%s' "$out"
}

process_stdin() {
  # Read all stdin into a variable while preserving newlines
  local input
  input="$(cat -)"

  # Trim leading and trailing whitespace while preserving internal newlines using sed
  local trimmed
  trimmed="$(printf '%s' "$input" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"

  # URL regex: check if trimmed content is a single-line URL
  # First check that there are no newlines in trimmed content, then check URL format
  if [[ "$trimmed" != *$'\n'* ]] && printf '%s' "$trimmed" | grep -Eq '^https?://[^[:space:]]+$'; then
    # It's a URL: call transformer and print its output only if successful
    local result
    if result="$(transform_url "$trimmed")"; then
      # Only print header and result if the command succeeds
      print_prompt
      printf '%s' "$result"
    else
      # transform_url failed, treat as regular input instead of exiting
      print_prompt
      printf '%s' "$input"
    fi
  else
    # Not a URL: print original stdin exactly with header
    print_prompt
    printf '%s' "$input"
  fi
}

process_stdin

