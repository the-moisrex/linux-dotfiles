#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt clipboard [--head N]

Read from the clipboard and embed the content as context for the AI.

Options:
  --head N   Keep only the first N lines of the embedded content
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

content="$(clipboard_content)"
if [[ -z "$content" ]]; then
    echo "prompt clipboard: Clipboard is empty or unavailable." >&2
    exit 1
fi

trim_context "$content"
