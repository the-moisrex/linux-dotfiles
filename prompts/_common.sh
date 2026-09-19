#!/bin/bash

# Prefill ARGS with the global script arguments
curfile="$0"
ARGS=("$@")
head_lines=""
GIT_ROOT=""
NO_FILES=${NO_FILES:="false"}
STDIN_CONSUMED=${STDIN_CONSUMED:="false"}
stdin_content="${stdin_content:=""}"

find_git_root() {
    if [[ -n "$GIT_ROOT" ]]; then
        return
    fi
    if git rev-parse --show-toplevel >/dev/null 2>&1; then
        GIT_ROOT="$(git rev-parse --show-toplevel)"
    else
        gitroot=".git"
        until [[ "$(realpath "$gitroot" 2>/dev/null)" == "/.git" ]] || \
              [[ "$(realpath "$gitroot" 2>/dev/null)" == "/" ]] || \
              [[ -d "$gitroot" ]]; do
            gitroot="../${gitroot}"
        done
        GIT_ROOT="$(basename "$gitroot/..")"
    fi
}


trim_context() {
    local content="$1"
    if [[ -n "$head_lines" ]]; then
        printf '%s\n' "$content" | head -n "$head_lines"
    else
        printf '%s\n' "$content"
    fi
}

# Read piped stdin into stdin_content without printing.
# Sets STDIN_CONSUMED=true and stdin_content on success.
# Returns 1 if stdin is not piped.
read_stdin() {
    stdin_content=""
    STDIN_CONSUMED=false
    if [ -t 0 ]; then
        return 1
    fi
    stdin_content="$(cat)"
    STDIN_CONSUMED=true
}

# Read piped stdin and print it as a fenced block.
# Calls read_stdin internally, then prints the content.
# Returns 1 if stdin is not piped.
embed_stdin() {
    read_stdin || return 1
    printf '%s\n\n' "$stdin_content"
}

# Check if ARGS has files. Returns 1 if ARGS is empty.
# Scripts that need interactive file selection should call select_files() directly.
get_files() {
    if [[ ${#ARGS[@]} -gt 0 ]]; then
        return 0
    fi
    return 1
}

# Usage:
#  parse_arguments
parse_arguments() {
    # Load the current ARGS array into the function's positional parameters ($1, $2, etc.)
    set -- "${ARGS[@]}"
    
    # Clear the global ARGS array to hold only the remaining (non-flag) arguments
    ARGS=()
    
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
                # Save the argument to the global ARGS array and shift past it
                ARGS+=("$1")
                shift
            ;;
        esac
    done
}

# --- Example of how the script flows ---
# init_prompt              # or: init_prompt --no-files
# get_files || true        # if files are needed
# embed_stdin || true      # if stdin content should be embedded
# echo "Files: ${ARGS[*]}"


infer_lang() {
    local file="$1"
    local base ext lang

    base="$(basename "$file")"
    ext="${base##*.}"

    case "$base" in
        Dockerfile) lang="dockerfile" ;;
        Makefile|makefile|GNUmakefile) lang="makefile" ;;
        CMakeLists.txt) lang="cmake" ;;
        *)
            case "$ext" in
                c|h) lang="c" ;;
                cc|cp|cpp|cxx|c++|hpp|hxx|hh|h++) lang="cpp" ;;
                m) lang="objectivec" ;;
                mm) lang="objective-cpp" ;;
                rs) lang="rust" ;;
                py|pyi) lang="python" ;;
                sh|bash) lang="bash" ;;
                zsh) lang="zsh" ;;
                fish) lang="fish" ;;
                nu) lang="nu" ;;
                js|cjs|mjs) lang="javascript" ;;
                ts|mts|cts) lang="typescript" ;;
                jsx) lang="jsx" ;;
                tsx) lang="tsx" ;;
                java) lang="java" ;;
                kt|kts) lang="kotlin" ;;
                swift) lang="swift" ;;
                go) lang="go" ;;
                rb) lang="ruby" ;;
                php) lang="php" ;;
                lua) lang="lua" ;;
                pl|pm) lang="perl" ;;
                r) lang="r" ;;
                scala) lang="scala" ;;
                cs) lang="csharp" ;;
                fs|fsx) lang="fsharp" ;;
                vb) lang="vbnet" ;;
                dart) lang="dart" ;;
                ex|exs) lang="elixir" ;;
                erl|hrl) lang="erlang" ;;
                clj|cljs|cljc) lang="clojure" ;;
                ml|mli) lang="ocaml" ;;
                sql) lang="sql" ;;
                html|htm) lang="html" ;;
                css) lang="css" ;;
                scss) lang="scss" ;;
                sass) lang="sass" ;;
                less) lang="less" ;;
                xml) lang="xml" ;;
                xsl|xslt) lang="xslt" ;;
                svg) lang="svg" ;;
                json) lang="json" ;;
                jsonc) lang="jsonc" ;;
                yaml|yml) lang="yaml" ;;
                toml) lang="toml" ;;
                ini|cfg|conf) lang="ini" ;;
                env) lang="dotenv" ;;
                md) lang="markdown" ;;
                txt|log) lang="text" ;;
                diff|patch) lang="diff" ;;
                proto) lang="proto" ;;
                asm|s|S) lang="asm" ;;
                tex) lang="tex" ;;
                vim) lang="vim" ;;
                *) lang="text" ;;
            esac ;;
    esac
    printf '%s' "$lang"
}

select_files() {
    local selected=""

    if ! command -v fzf >/dev/null 2>&1; then
        printf 'prompt clang-tidy: fzf is required when no files are specified\n' >&2
        exit 1
    fi

    find_git_root

    if [[ -n "${GIT_ROOT:-}" ]]; then
        selected="$(
            cd "$GIT_ROOT" &&
            git ls-files --cached --others --exclude-standard | fzf -m
        )"
    else
        selected="$(rg --files 2>/dev/null || find . -type f | fzf -m)"
    fi

    if [[ -z "$selected" ]]; then
        exit 0
    fi

    printf '%s\n' "$selected"
}

resolve_input_file() {
    local file="$1"
    
    if [[ -f "$file" ]]; then
        printf '%s\n' "$file"
        return 0
    fi

    find_git_root
    
    if [[ -n "${GIT_ROOT:-}" && -f "$GIT_ROOT/$file" ]]; then
        printf '%s\n' "$GIT_ROOT/$file"
        return 0
    fi

    if command -v fzf >/dev/null; then
        local selected=""
        if [[ -n "${GIT_ROOT:-}" ]]; then
            selected="$(
                cd "$GIT_ROOT" &&
                git ls-files --cached --others --exclude-standard | fzf -f "$file" | head -n 1
            )"
        else
            selected="$(rg --files 2>/dev/null || find . -type f | fzf -f "$file" | head -n 1)"
        fi
        if [[ -f "$selected" ]]; then
            printf '%s\n' "$selected"
            return 0
        fi
    fi

    return 1
}



# Read clipboard content. Scripts that need clipboard call this explicitly.
# Returns empty string silently if clipboard is unavailable or empty.
clipboard_content() {
    local script_dir
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    "$script_dir/../bin/c.p" 2>/dev/null || true
}

# Initialize a prompt script. Replaces the common boilerplate:
#   source "$(dirname "$0")/_common.sh"
#   parse_arguments
#
# Usage:
#   init_prompt              # standard: parse args
#   init_prompt --no-files   # same, skip file selection/fallback
init_prompt() {
    if [[ "${1:-}" == "--no-files" ]]; then
        NO_FILES=true
    fi
    parse_arguments
}

embed_file() {
    local path="$1"
    local label="${2:-}"
    local name
    
    if [[ ! -f "$path" ]]; then
        echo "Warning: context file not found: $path" >&2
        return 1
    fi
    
    name="$(basename "$path")"
    label="${label:-$name}"
    
    echo
    echo "File: $label"
    echo "\`\`\`$(infer_lang "$name")"
    trim_context "$(cat -- "$path")"
    echo '```'
}

