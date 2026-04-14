#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt commit [--head N] [FILE]
       some-command | prompt commit [--head N] [FILE]

Write a strong git commit message for this change.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

trim_context() {
  local content="$1"
  if [[ -n "$head_lines" ]]; then
    printf '%s' "$content" | head -n "$head_lines"
  else
    printf '%s' "$content"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      show_help
      exit 0
      ;;
    --head)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --head" >&2
        exit 2
      fi
      head_lines="$2"
      shift 2
      ;;
    *)
      break
      ;;
  esac
done

if ! [ -t 0 ]; then
  stdin_piped=true
  stdin_content="$(cat)"
fi

if $stdin_piped && [[ -n "$stdin_content" ]]; then
  printf '%s

' "$stdin_content"
fi

echo "Write a strong git commit message for this change."
echo "Provide:"
echo "1. A concise subject line."
echo "2. A short body explaining what changed and why."
echo "3. If helpful, an alternative subject line."
echo "Keep it specific and practical, not generic."
echo

if [[ $# -gt 0 && -f "$1" ]]; then
  file_name="$(basename "$1")"
  echo "File: $file_name"
  echo
  echo '```'
  trim_context "$(cat -- "$1")"
elif $stdin_piped; then
  echo '```'
  trim_context "$stdin_content"
else
  echo '```'
fi

echo
echo '```'
