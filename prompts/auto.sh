#!/usr/bin/env bash
set -euo pipefail

show_help() {
    cat <<'EOF'
Usage: prompt auto [FILE...]
       some-command | prompt auto [FILE...]

Automatically chooses and executes the most appropriate prompt script based on the input.
For example, if it detects YouTube URLs, it delegates to the 'yt' prompt.
If it detects C++ files, it delegates to 'cpp-reviewer'.
Defaults to 'review' if no specific pattern is matched.

Options:
  --help, -h   Show this help message
  (Any other options like --head are passed through to the selected script)
EOF
}

# Fast-path for help to avoid consuming stdin unnecessarily
for arg in "$@"; do
    if [[ "$arg" == "--help" || "$arg" == "-h" ]]; then
        show_help
        exit 0
    fi
done

target_script="review.sh"
input_buffer=""
has_stdin=false

# Buffer stdin if it is provided via pipe
if ! [ -t 0 ]; then
    has_stdin=true
    input_buffer="$(cat)"
fi

# Heuristic 1: Check stdin buffer for clues
if $has_stdin; then
    if echo "$input_buffer" | grep -qE 'youtube\.com|youtu\.be'; then
        target_script="yt.sh"
        elif echo "$input_buffer" | grep -qE 'class |struct |#include <'; then
        target_script="cpp-reviewer.sh"
    fi
fi

# Heuristic 2: Check arguments for file extensions or direct URLs (overrides stdin)
for arg in "$@"; do
    if [[ "$arg" == *"youtube.com"* || "$arg" == *"youtu.be"* ]]; then
        target_script="yt.sh"
        break
        elif [[ -f "$arg" ]]; then
        ext="${arg##*.}"
        case "$ext" in
            cpp|hpp|cxx|hxx|cc|c|h)
                target_script="cpp-reviewer.sh"
            ;;
            sh|bash)
                target_script="review.sh"
            ;;
        esac
    fi
done

script_dir="$(dirname "$0")"
target_path="$script_dir/$target_script"

if [[ ! -x "$target_path" && ! -f "$target_path" ]]; then
    # Fallback if the chosen script doesn't exist
    target_path="$script_dir/review.sh"
fi

# Execute the chosen script, passing along the buffered stdin and all arguments
if $has_stdin; then
    printf '%s\n' "$input_buffer" | bash "$target_path" "$@"
else
    exec bash "$target_path" "$@"
fi
