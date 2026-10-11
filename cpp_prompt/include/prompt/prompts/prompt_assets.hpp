#pragma once

// Static snapshots of the bash prompt system's shared infrastructure,

// embedded so this binary never reads the prompts/ directory.

namespace prompt {

namespace prompt_assets {

inline constexpr char const* common =
    R"PROMPT_COMMON(#!/bin/bash

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
        # sed (unlike head) always drains its input, so printf never dies of
        # SIGPIPE when truncating a large context under `set -o pipefail`.
        printf '%s\n' "$content" | sed -n "1,${head_lines}p"
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
                if [[ ! "$2" =~ ^[0-9]+$ ]]; then
                    echo "--head requires a non-negative integer" >&2
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
                json|stock|tse) lang="json" ;;
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

# Path of a file relative to the git root when inside one, else to $PWD.
# Keeps embedded headings short and free of $HOME (sanitize_output rewrites
# absolute home paths in prompt output).
relative_path() {
    local file="$1"
    find_git_root
    if [[ -n "${GIT_ROOT:-}" ]]; then
        realpath --relative-to="$GIT_ROOT" "$file"
    else
        realpath --relative-to="$PWD" "$file"
    fi
}

# Resolve a file exactly (cwd, then git root) without fzf's fuzzy fallback —
# for snapshot inputs where a fuzzy match would silently pick the wrong file.
resolve_exact_file() {
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
    return 1
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

# Print the first stock identifier found in the clipboard: an easytrader/
# tsetmc/codal URL, an ISIN (12 chars), a TSETMC insCode (15+ digits), or a
# plain non-Latin symbol (e.g. فولاد). Used by prompts that fall back to
# the clipboard when no symbol argument is given. Returns 1 when the
# clipboard holds no recognizable identifier.
clipboard_identifier() {
    local text tok
    local -a toks
    text="$(clipboard_content)"
    text="${text//$'\r'/ }"
    text="${text//$'\n'/ }"
    [[ -z "${text//[[:space:]]/}" ]] && return 1
    read -r -a toks <<< "$text"
    for tok in "${toks[@]}"; do
        tok="${tok//\"/}"
        tok="${tok//\'/}"
        [[ -z "$tok" ]] && continue
        case "$tok" in
            http://*|https://*)
                if [[ "$tok" =~ (easytrader\.ir|tsetmc\.com|codal\.ir)/ ]]; then
                    printf '%s\n' "$tok"
                    return 0
                fi
                continue
            ;;
        esac
        if [[ "$tok" =~ ^[A-Za-z]{2}[A-Za-z0-9]{10}$ ]] || [[ "$tok" =~ ^[0-9]{15,}$ ]]; then
            printf '%s\n' "$tok"
            return 0
        fi
    done
    text="${text#"${text%%[![:space:]]*}"}"
    text="${text%"${text##*[![:space:]]}"}"
    if [[ -n "$text" && ${#text} -le 64 && ! "$text" =~ [A-Za-z0-9] ]]; then
        printf '%s\n' "$text"
        return 0
    fi
    return 1
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

# --- Prompt discovery -------------------------------------------------
# Mirrors the search order used by the `prompt` dispatcher:
# $XDG_CONFIG_DIRS/prompts first, then this prompts directory.

COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

prompt_search_dirs() {
    local dirs="${XDG_CONFIG_DIRS:-/etc/xdg}" d
    local -a xdg=()
    IFS=':' read -r -a xdg <<< "$dirs"
    for d in "${xdg[@]}"; do
        [[ -z "$d" ]] && continue
        printf '%s\n' "${d%/}/prompts"
    done
    printf '%s\n' "$COMMON_DIR"
}

# Enumerate available prompts. Prints unique "name<TAB>file" lines in
# search order (first match wins), skipping _-prefixed files and
# stripping known extensions.
collect_prompts() {
    local dir file base name ext
    local -A seen=()
    while IFS= read -r dir; do
        [[ -d "$dir" ]] || continue
        while IFS= read -r file; do
            [[ -f "$file" ]] || continue
            base="$(basename "$file")"
            [[ "$base" == _* ]] && continue
            name="$base"
            for ext in .sh .txt .md; do
                name="${name%$ext}"
            done
            name="${name%.}"
            if [[ -n "$name" && -z "${seen[$name]:-}" ]]; then
                seen[$name]=1
                printf '%s\t%s\n' "$name" "$file"
            fi
        done < <(find "$dir" -maxdepth 1 -type f -print 2>/dev/null)
    done < <(prompt_search_dirs)
}

# Statically extract show_help() text from prompt scripts with
# _extract-help.awk — one awk run replaces running `bash file --help` for
# every prompt (the dominant cost of `prompt list`).
#   extract_help para <file>...  one line per file:
#                                <file>\x1f<first paragraph> when a static
#                                heredoc exists (paragraph may be empty),
#                                a bare <file> line otherwise (no static
#                                help — caller falls back to `bash --help`).
#   extract_help full <file>...  records: \x1c<file> header line (ASCII 28)
#                                followed by the help body; an empty body
#                                means fall back to `bash --help`.
extract_help() {
    local mode="$1"
    shift
    (($#)) || return 0
    awk -v mode="$mode" -f "$COMMON_DIR/_extract-help.awk" "$@"
}

)PROMPT_COMMON";

inline constexpr char const* fix =
    R"PROMPT_FIX(#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt fix [--head N] [FILE...]
       some-command | prompt fix [--head N] [FILE...]

Find the root problem here and propose the smallest useful fix.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}


source "$(dirname "$0")/_common.sh"
init_prompt
get_files || true


echo "Find the root problem here and propose the smallest useful fix."
echo "Explain the issue briefly, mention any important assumptions, and provide the answer primarily as a git diff that can be applied directly."
echo "Prefer minimal, surgical changes over broad rewrites."
echo

# Iterate through all collected file arguments
for file in "${ARGS[@]}"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
    else
        # Optional: warn if a passed file does not exist
        echo "Warning: File '$file' not found." >&2
    fi
done
)PROMPT_FIX";

inline constexpr char const* symbols =
    R"PROMPT_SYMBOLS(#!/usr/bin/env bash

show_help() {
    cat <<'EOF'
Usage: prompt symbols [--head N] [file...]

Builds a prompt that reviews symbol names.
If file paths are given, it reads those files; otherwise it reads stdin.
Example: `prompt symbols $(fzf)`

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt


echo "Review the symbols in this code and find bad names that should be renamed."
echo "Focus on unclear, misleading, overly abbreviated, inconsistent, or non-idiomatic symbol names."
echo "For each rename suggestion, provide the current name, the proposed new name, and the reason in a table."
echo "Only suggest renames that materially improve readability or maintainability."

if [[ $# -gt 0 ]]; then
    echo "At the end, provide a git diff that applies the renames in the provided files."
else
    echo "At the end, provide a git diff that applies the renames."
fi


# Iterate through all the collected files
for file_path in "${ARGS[@]}"; do
    if [[ -f "$file_path" ]]; then
        file=$(basename "$file_path")
        echo
        echo "File: $file"
        echo "\`\`\`$(infer_lang "$file")"
        trim_context "$(cat -- "$file_path")"
        echo
        echo '```'
    else
        echo "Warning: File not found or is not a regular file: $file_path" >&2
    fi
done
)PROMPT_SYMBOLS";

} // namespace prompt_assets

} // namespace prompt
