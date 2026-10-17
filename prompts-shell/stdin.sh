#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt stdin [--head N]

Read from stdin and embed the content as context for the AI.
Pipe something into this prompt to include it in the AI context.

Options:
  --head N   Keep only the first N lines of the embedded content
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

if ! embed_stdin; then
    echo "prompt stdin: No input. Pipe something to stdin." >&2
    exit 1
fi
