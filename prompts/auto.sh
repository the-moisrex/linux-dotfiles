#!/usr/bin/env bash
set -euo pipefail

show_help() {
    cat <<'EOF'
Usage: prompt auto [FILE...]
       some-command | prompt auto [FILE...]

Automatically chooses and executes the most appropriate prompt script based on the input.
For example, if it detects YouTube URLs, it delegates to the 'yt' prompt.
If it detects C++ files, it delegates to 'cpp-reviewer'.
Defaults to 'summarize' for English and Farsi text, or 'english' (translate) for other languages like Arabic.

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

target_script="summarize.sh"
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
    else
        # Determine if the text should be translated to English.
        # Languages we should NOT translate (use summarize): English, Farsi
        # All other languages (including Arabic, Chinese, Russian, accented European languages, etc.)
        # are translated via english.sh.
        #
        # Special characters, emojis, punctuation, symbols, numbers, whitespace, and
        # bidirectional/format marks are ignored and do not trigger translation.
        # Only letter characters (\p{L}) are examined.
        #
        # - Pure ASCII letters (a-zA-Z) => English => summarize
        # - Arabic-script letters without Arabic-only forms => Farsi => summarize
        # - Anything else (other scripts, or Arabic-specific letters) => translate
        # -CSD ensures Perl treats stdin/stdout and the script as UTF-8.
        if printf '%s\n' "$input_buffer" | perl -CSD -e '
            while (<>) {
                while (/(\p{L})/g) {
                    my $char = $1;
                    # Letter outside basic English ASCII and outside Arabic script
                    if ($char !~ /^[a-zA-Z]$/ && $char !~ /\p{Script=Arabic}/) {
                        exit 0;
                    }
                    # Arabic-specific letters not standard in Farsi:
                    # ة (U+0629), ي (U+064A), ك (U+0643)
                    if ($char =~ /[\x{0629}\x{064A}\x{0643}]/) {
                        exit 0;
                    }
                }
            }
            exit 1;
        '; then
            target_script="english.sh"
        else
            target_script="summarize.sh"
        fi
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
