#!/usr/bin/env bash
set -euo pipefail

stdin_piped=false
stdin_content=""
head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt explain [--head N] [FILE]
       some-command | prompt explain [--head N] [FILE]

Explain this clearly and concretely.

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

echo "Explain this clearly and concretely."
echo "Describe what it does, how the parts fit together, key control flow, important assumptions, and likely failure points."
echo "If useful, finish with a small git diff that improves readability through naming, structure, or comments."
echo

if [[ $# -gt 0 && -f "$1" ]]; then
  file_name="$(basename "$1")"
  echo "File: $file_name"
  echo
  echo '```'
  trim_context "$(cat -- "$1")"
  echo
  echo '```'
fi
