#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt cli [--head N] <command> [args...]
       some-input | prompt cli [--head N] <command> [args...]

Executes the given CLI command and appends its output as a Markdown code block.
Useful for appending the output of arbitrary commands to your prompt chain.

Options:
  --head N   Keep only the first N lines of the command output
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

if [ $# -eq 0 ]; then
    printf 'prompt cli: no command provided\n' >&2
    show_help >&2
    exit 1
fi

# Reconstruct the command as a string for the description
cmd_description="$*"

# Temporarily disable exit on error so we can capture failed commands gracefully
set +e
# Use eval so pipes, redirects, and quotes work correctly
cmd_output="$(eval "$@" 2>&1)"
cmd_status=$?
set -e

printf 'Output of command `%s`:\n\n' "$cmd_description"

if [[ $cmd_status -ne 0 ]]; then
    printf '(Exited with status: %s)\n\n' "$cmd_status"
fi

printf '
```text\n'
if [[ -n "$cmd_output" ]]; then
    trim_context "$cmd_output"
fi
printf '\n
```\n'
