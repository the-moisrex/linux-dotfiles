#!/bin/bash

# Prefill ARGS with the global script arguments
ARGS=("$@")
head_lines=""

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
    if ! [ -t 0 ]; then
        stdin_piped=true
        stdin_content="$(cat)"
    fi
    
    if $stdin_piped && ! [ -v FROM_CLIPBOARD ] && [[ -n "$stdin_content" ]]; then
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



common_behavior() {
    parse_arguments
    print_stdin
}