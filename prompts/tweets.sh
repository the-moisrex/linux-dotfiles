#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false

show_help() {
  cat <<'EOF'
Usage: prompt tweets [--head N] [FILE...]
       some-command | prompt tweets [--head N] [FILE...]

Generate tweet ideas from the provided content.
Produce a table showing each tweet, why it works, and its expected impact.
No hashtags or weird emojis.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

NO_FILES=true
source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Based on the input content, generate several high-quality tweet ideas."
echo "Create a markdown table with columns: Tweet | Why this tweet | Expected impact"
echo "Write clean, natural tweets only — no hashtags and no weird emojis."
echo "Focus on clear, engaging language that fits the source material."
echo "Prefer 3–5 strong, ready-to-post ideas. Keep them concise."
echo

