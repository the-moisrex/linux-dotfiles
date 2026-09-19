#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt farsi [--head N]...
       some-command | prompt farsi [--head N]...

Translate to Farsi.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt

echo "Translate to Farsi; no explanations, no comments, no extra text, just the translation."
echo
cat
