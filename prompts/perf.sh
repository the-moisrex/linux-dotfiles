#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt perf [--head N] [FILE]
       some-command | prompt perf [--head N] [FILE]

Review this for performance issues.

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

echo "Review this for performance issues."
echo "Look for unnecessary allocations, wasteful copies, bad algorithms, blocking work, repeated parsing, excessive syscalls, poor data layout, and avoidable hot-path overhead."
echo "Call out which issues matter most in practice, and provide a git diff for the smallest high-impact improvements."
echo "Do not sacrifice correctness or readability for tiny wins."
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
