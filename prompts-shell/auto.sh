#!/usr/bin/env bash

show_help() {
    cat <<'EOF'
Usage: prompt auto [FILE...]
       some-command | prompt auto [FILE...]

Automatically chooses and executes the most appropriate prompt script based on the input.
For example, if it detects YouTube URLs, it delegates to the 'yt' prompt.
If it detects C++ files, it delegates to 'cpp-reviewer'.
A bare stock identifier (ISIN, insCode, or easytrader/tsetmc/codal URL) delegates to 'stock'.
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
stock_identifier=""


# Re-usable function to check if a given string looks like C++ compiler/linker output
is_compiler_output() {
    local content="$1"
    # Matches GCC/Clang standard error formats (with or without column numbers),
    # include paths, common linker errors, and build system failures (Ninja/Make).
    if echo "$content" | grep -qiE \
    -e ':[0-9]+:([0-9]+:)? (error|warning|fatal error|note):' \
    -e 'In file included from' \
    -e 'undefined reference to' \
    -e 'no matching function for call to' \
    -e 'ld: symbol\(s\) not found' \
    -e 'FAILED:' \
    -e 'ninja: build stopped' \
    -e 'make\[[0-9]+\]: \*\*\*'; then
        return 0
    fi
    return 1
}


# Check whether the content is a single Iranian stock identifier:
# an ISIN (IR + 10 alphanumerics), a TSETMC insCode (15+ digits), or an
# easytrader/tsetmc/codal URL (matching bin/tse normalize_input patterns).
# The whole buffer must be exactly that one identifier (trimmed); on
# success the trimmed value is stored in $stock_identifier.
is_stock_identifier() {
    local content="$1"
    local re_isin='^IR[A-Za-z0-9]{10}$'
    local re_inscode='^[0-9]{15,}$'
    local re_easytrader='^https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12}(/|\?|$)'
    local re_tsetmc='^https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,}(/|\?|$)'
    local re_codal='^https?://(www\.)?codal\.ir/ReportList\.aspx\?([^[:space:]]*&)?Symbol=[^&[:space:]]+'
    content="${content#"${content%%[![:space:]]*}"}"
    content="${content%"${content##*[![:space:]]}"}"
    [[ -z "$content" || "$content" == *$'\n'* ]] && return 1
    if [[ "$content" =~ $re_isin ]] ||
    [[ "$content" =~ $re_inscode ]] ||
    [[ "$content" =~ $re_easytrader ]] ||
    [[ "$content" =~ $re_tsetmc ]] ||
    [[ "$content" =~ $re_codal ]]; then
        stock_identifier="$content"
        return 0
    fi
    return 1
}

# Extract the first stock identifier found anywhere in the input (multi-line ok).
extract_stock_identifier() {
    local content="$1"
    local re_isin='IR[A-Za-z0-9]{10}'
    local re_inscode='[0-9]{15,}'
    local re_easytrader='https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12}'
    local re_tsetmc='https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,}'
    local re_codal='https?://(www\.)?codal\.ir/ReportList\.aspx\?([^[:space:]]*&)?Symbol=[^&[:space:]]+'
    local match
    # grep's status must be the test result: piping straight into head
    # would mask it (head exits 0 even when grep found nothing, so every
    # input used to "match" and plain text was routed to stock).
    match=$(echo "$content" | grep -oE "$re_easytrader|$re_tsetmc|$re_codal|$re_isin|$re_inscode" || true)
    match="${match%%$'\n'*}"
    if [[ -n "$match" ]]; then
        stock_identifier="$match"
        return 0
    fi
    return 1
}

source "$(dirname "$0")/_common.sh"

# Buffer stdin if it is provided via pipe
if ! [ -t 0 ]; then
    has_stdin=true
    input_buffer="$(cat)"
    input_buffer="${input_buffer%%$'\n'}"
    input_buffer="${input_buffer%%$'\r'}"
fi

if [ -z "$input_buffer" ]; then
    input_buffer="$(clipboard_content)";
fi

echo "$input_buffer"

# Heuristic 1: Check stdin buffer for clues
if [ ! -z "$input_buffer" ]; then
    if is_compiler_output "$input_buffer"; then
        target_script="cpp.sh"
        elif echo "$input_buffer" | grep -qE 'youtube\.com|youtu\.be'; then
        target_script="yt.sh"
        elif echo "$input_buffer" | grep -qiE '^FROM |^RUN |^COPY |^CMD |^ENTRYPOINT |^docker-compose|^services:'; then
        target_script="docker.sh"
        elif echo "$input_buffer" | grep -qiE '^\.github/workflows/|\.gitlab-ci\.yml|Jenkinsfile|stages:|jobs:'; then
        target_script="ci.sh"
        elif echo "$input_buffer" | grep -qiE 'Traceback \(most recent call last\)|File ".*", line|Fatal error|panic:|Segmentation fault|stack trace|Exception in'; then
        target_script="debug.sh"
        elif is_stock_identifier "$input_buffer"; then
        target_script="stock.sh"
        elif extract_stock_identifier "$input_buffer"; then
        target_script="stock.sh"
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
    
    # If running interactively (no stdin piped), check clipboard for stock identifier
    if ! $has_stdin; then
        if clipboard_identifier >/dev/null 2>&1; then
            target_script="stock.sh"
        fi
    fi
fi

# Heuristic 2: Check arguments for file extensions or direct URLs (overrides stdin)
for arg in "$@"; do
    if is_stock_identifier "$arg"; then
        target_script="stock.sh"
        stock_identifier=""
        break
        elif [[ "$arg" == *"youtube.com"* || "$arg" == *"youtu.be"* ]]; then
        target_script="yt.sh"
        break
        elif [[ -f "$arg" ]]; then
        base="$(basename "$arg")"
        ext="${arg##*.}"
        case "$ext" in
            cpp|hpp|cxx|hxx|cc|c|h)
                target_script="cpp-reviewer.sh"
            ;;
            sh|bash)
                target_script="review.sh"
            ;;
            py|pyi|rb|go|rs|java|kt|swift|ts|js)
                target_script="review.sh"
            ;;
            yml|yaml)
                case "$base" in
                    docker-compose*|compose*)
                        target_script="docker.sh"
                    ;;
                    .gitlab-ci*|*.gitlab-ci*)
                        target_script="ci.sh"
                    ;;
                    *)
                        target_script="review.sh"
                    ;;
                esac
            ;;
            json)
                target_script="review.sh"
            ;;
            md)
                target_script="readme.sh"
            ;;
        esac
        # Check filename patterns regardless of extension
        case "$base" in
            Dockerfile|dockerfile|*.dockerfile)
                target_script="docker.sh"
            ;;
            Jenkinsfile|.gitlab-ci.yml|Makefile|CMakeLists.txt)
                target_script="review.sh"
            ;;
            .github/workflows/*.yml)
                target_script="ci.sh"
            ;;
        esac
    fi
done

script_dir="$(dirname "$0")"
target_path="$script_dir/$target_script"

if [[ ! -x "$target_path" && ! -f "$target_path" ]]; then
    # Fallback if the chosen script doesn't exist
    target_path="$script_dir/review.sh"
    stock_identifier=""
fi

# A stock identifier detected from stdin is handed to stock.sh as an
# argument (stock.sh does not read stdin); skip it when the target
# changed or the identifier already came from the arguments.
extra_args=()
if [[ "$target_script" == "stock.sh" && -n "$stock_identifier" ]]; then
    extra_args=("$stock_identifier")
fi

# Execute the chosen script, passing along the buffered stdin and all arguments
if $has_stdin; then
    printf '%s\n' "$input_buffer" | bash "$target_path" "${extra_args[@]}" "$@"
else
    exec bash "$target_path" "$@"
fi
