#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt man [--head N] <page>...
       some-input | prompt man [--head N] <page>...

Fetches the manual page for the given command(s) and appends it as a Markdown code block.
This is essentially a shorthand for `prompt cli man -P cat <page>`.

Options:
  --head N   Keep only the first N lines of the man page output
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

if [ $# -eq 0 ]; then
    printf 'prompt man: no manual page provided\n' >&2
    show_help >&2
    exit 1
fi

# Reconstruct the arguments for the description
man_pages="$*"

# Temporarily disable exit on error to capture failed man lookups gracefully
set +e
# Use -P cat to output plain text instead of using a pager
man_output="$(man -P cat "$@" 2>&1)"
man_status=$?
set -e

printf 'Manual page for `%s`:\n\n' "$man_pages"

if [[ $man_status -ne 0 ]]; then
    printf '(Exited with status: %s)\n\n' "$man_status"
fi

printf '```text\n'
if [[ -n "$man_output" ]]; then
    trim_context "$man_output"
fi
printf '```\n\n'
