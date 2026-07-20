#!/bin/bash

# Prefill ARGS with the global script arguments
curfile="$0"
ARGS=("$@")
head_lines=""
GIT_ROOT=""
NO_FILES=false
STDIN_CONSUMED=false

find_git_root() {
    if [ ! -z "$GIT_ROOT" ]; then
        return
    fi
    if git rev-parse --show-toplevel >/dev/null 2>&1; then
        GIT_ROOT="$(git rev-parse --show-toplevel)"
    else
        gitroot=".git";
        until [ "$(realpath "$gitroot" 2>/dev/null)" = "/.git" ] || \
            [ "$(realpath "$gitroot" 2>/dev/null)" = "/" ] || \
            [ -d "$gitroot" ]; do
            gitroot="../${gitroot}";
        done;
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

# Usage:
#   print_stdin
print_stdin() {
    local stdin_piped=false
    local stdin_content=""

    if $NO_FILES; then
        return
    fi

    if ! [ -t 0 ]; then
        stdin_piped=true
        stdin_content="$(cat)"
    fi
    
    if $stdin_piped && ! [ -v FROM_CLIPBOARD ] && [[ -n "$stdin_content" ]]; then
        STDIN_CONSUMED=true
        printf '%s\n\n' "$stdin_content"
        # Check the length of the global ARGS array instead of $#
    elif [[ ${#ARGS[@]} -eq 0 ]]; then
        if command -v fzf >/dev/null; then
            mapfile -t ARGS < <(git ls-files | fzf -m)
        else
            echo "No input files and fzf is not installed." >&2
            return 1
        fi
    fi
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
# parse_arguments
# print_stdin
# set -- "${ARGS[@]}"
# echo "Remaining files to process: $@"


infer_lang() {
    local file="$1"
    local base ext
    
    base="$(basename "$file")"
    ext="${base##*.}"
    
    case "$base" in
        Dockerfile) echo "dockerfile" ;;
        Makefile|makefile|GNUmakefile) echo "makefile" ;;
        CMakeLists.txt) echo "cmake" ;;
        *) case "$ext" in
                c) echo "c" ;;
                h) echo "c" ;;
                cc|cp|cpp|cxx|c++|hpp|hxx|hh|h++) echo "cpp" ;;
                m) echo "objectivec" ;;
                mm) echo "objective-cpp" ;;
                rs) echo "rust" ;;
                py|pyi) echo "python" ;;
                sh|bash) echo "bash" ;;
                zsh) echo "zsh" ;;
                fish) echo "fish" ;;
                nu) echo "nu" ;;
                js|cjs|mjs) echo "javascript" ;;
                ts|mts|cts) echo "typescript" ;;
                jsx) echo "jsx" ;;
                tsx) echo "tsx" ;;
                java) echo "java" ;;
                kt|kts) echo "kotlin" ;;
                swift) echo "swift" ;;
                go) echo "go" ;;
                rb) echo "ruby" ;;
                php) echo "php" ;;
                lua) echo "lua" ;;
                pl|pm) echo "perl" ;;
                r) echo "r" ;;
                scala) echo "scala" ;;
                cs) echo "csharp" ;;
                fs|fsx) echo "fsharp" ;;
                vb) echo "vbnet" ;;
                dart) echo "dart" ;;
                ex|exs) echo "elixir" ;;
                erl|hrl) echo "erlang" ;;
                clj|cljs|cljc) echo "clojure" ;;
                ml|mli) echo "ocaml" ;;
                sql) echo "sql" ;;
                html|htm) echo "html" ;;
                css) echo "css" ;;
                scss) echo "scss" ;;
                sass) echo "sass" ;;
                less) echo "less" ;;
                xml) echo "xml" ;;
                xsl|xslt) echo "xslt" ;;
                svg) echo "svg" ;;
                json) echo "json" ;;
                jsonc) echo "jsonc" ;;
                yaml|yml) echo "yaml" ;;
                toml) echo "toml" ;;
                ini|cfg|conf) echo "ini" ;;
                env) echo "dotenv" ;;
                md) echo "markdown" ;;
                txt|log) echo "text" ;;
                diff|patch) echo "diff" ;;
                proto) echo "proto" ;;
                asm|s|S) echo "asm" ;;
                tex) echo "tex" ;;
                vim) echo "vim" ;;
                *) echo "text" ;;
        esac ;;
    esac
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

    while IFS= read -r file; do
        [[ -n "$file" ]] && echo "$file"
    done <<< "$selected"
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
        if [[ -n "${GIT_ROOT:-}" ]]; then
            selected="$(
                cd "$GIT_ROOT" &&
                git ls-files --cached --others --exclude-standard | fzf -f "$file" | head -n 1
            )"
        else
            selected="$(rg --files 2>/dev/null || find . -type f | fzf -f "$file" | head -n 1)"
        fi
        if [ -f "$selected" ]; then
            echo "$selected"
            return 0
        fi
    fi

    return 1
}



common_behavior() {
    parse_arguments
    print_stdin
}

embed_file() {
    local path="$1"
    local label="${2:-$(basename "$path")}"
    
    if [[ ! -f "$path" ]]; then
        echo "Warning: context file not found: $path" >&2
        return 1
    fi
    
    local name
    name="$(basename "$path")"
    
    echo
    echo "File: $label"
    echo "\`\`\`$(infer_lang "$name")"
    trim_context "$(cat -- "$path")"
    echo '```'
}

