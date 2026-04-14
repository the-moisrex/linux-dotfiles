#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""

show_help() {
  cat <<'EOF'
Usage: prompt security [FILE]
       some-command | prompt security [FILE]

Builds a prompt that reviews code or text for security and safety issues.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  show_help
  exit 0
fi

if ! [ -t 0 ]; then
  stdin_piped=true
  stdin_content="$(cat)"
fi

if $stdin_piped && [[ -n "$stdin_content" ]]; then
  printf '%s\n\n' "$stdin_content"
fi

echo "Review this for security and safety issues."
echo "Look for vulnerabilities, unsafe defaults, trust boundary mistakes, injection risks, missing validation, secrets exposure, privilege problems, dangerous file or process handling, and denial-of-service risks."
echo "Also call out reliability or safety hazards that could cause data loss, corruption, crashes, or harmful behavior."
echo "List findings by severity, explain the risk briefly, and suggest the smallest effective fix for each one."
echo

if [[ $# -gt 0 && -f "$1" ]]; then
  file_name="$(basename "$1")"
  echo "File: $file_name"
  echo
  echo '```'
  cat -- "$1"
elif $stdin_piped; then
  echo '```'
  printf '%s' "$stdin_content"
else
  echo '```'
fi

echo
echo '```'
