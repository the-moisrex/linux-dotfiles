#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt paths [--context N] [--full] [--head N] [FILE...]
       make 2>&1 | prompt paths [--context N] [--full] [--head N]

Extracts file paths from stdin (via bin/paths) and/or arguments,
resolves each, and embeds only Git-tracked files.

Modes:
  (default)   Show N lines of context around each file:line:col hit
  --full      Embed full file contents (delegates to files.sh)

Options:
  --context N  Lines of context before/after each hit (default: 10)
  --full       Embed full files instead of context windows
  --head N     Keep only the first N lines (full mode only)
EOF
}

NO_FILES=true
source "$(dirname "$0")/_common.sh"

mode="context"
context_lines=10

set -- "${ARGS[@]}"
ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            show_help
            exit 0
        ;;
        --context)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --context" >&2
                exit 2
            fi
            context_lines="$2"
            shift 2
        ;;
        --full)
            mode="full"
            shift
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
            ARGS+=("$1")
            shift
        ;;
    esac
done
set -- "${ARGS[@]}"

embed_stdin || true

BIN_PATHS="$(dirname "$0")/../bin/paths"
FILES_PROMPT="$(dirname "$0")/files.sh"

relative_path() {
    local file="$1"
    find_git_root
    if [[ -n "${GIT_ROOT:-}" ]]; then
        realpath --relative-to="$GIT_ROOT" "$file"
    else
        realpath --relative-to="$PWD" "$file"
    fi
}

declare -A file_lines=()
declare -A seen_files=()

if [[ -n "$stdin_content" ]]; then
    while IFS= read -r raw; do
        [[ -z "$raw" ]] && continue
        clean="${raw%%:*}"
        clean="${clean%:}"
        rest="${raw#"${clean}"}"
        line_num=""
        if [[ "$rest" =~ ^:([0-9]+) ]]; then
            line_num="${BASH_REMATCH[1]}"
        fi
        if [[ -z "${seen_files[$clean]+_}" ]]; then
            seen_files[$clean]=1
            file_lines[$clean]="${line_num}"
        fi
    done < <("$BIN_PATHS" <<< "$stdin_content")
fi

for raw in "$@"; do
    clean="${raw%%:*}"
    clean="${clean%:}"
    rest="${raw#"${clean}"}"
    line_num=""
    if [[ "$rest" =~ ^:([0-9]+) ]]; then
        line_num="${BASH_REMATCH[1]}"
    fi
    if [[ -z "${seen_files[$clean]+_}" ]]; then
        seen_files[$clean]=1
        file_lines[$clean]="${line_num}"
    fi
done

if [[ ${#seen_files[@]} -eq 0 ]]; then
    echo "prompt paths: no file paths found" >&2
    exit 1
fi

resolved_files=()
for file in "${!seen_files[@]}"; do
    if resolved="$(resolve_input_file "$file" 2>/dev/null)" && \
       [[ -r "$resolved" ]] && \
       git ls-files --error-unmatch "$resolved" &>/dev/null; then
        resolved_files+=("$resolved")
    fi
done

if [[ ${#resolved_files[@]} -eq 0 ]]; then
    echo "prompt paths: no tracked files found" >&2
    exit 1
fi

if [[ "$mode" == "full" ]]; then
    bash "$FILES_PROMPT" "${resolved_files[@]}"
else
    for resolved in "${resolved_files[@]}"; do
        rel="$(relative_path "$resolved")"
        lang="$(infer_lang "$resolved")"
        line_num="${file_lines[$resolved]:-}"

        if [[ -n "$line_num" ]]; then
            start=$(( line_num - context_lines ))
            (( start < 1 )) && start=1
            end=$(( line_num + context_lines ))
            total=$(wc -l < "$resolved")
            (( end > total )) && end=$total

            printf 'File %s around line %d (lines %d–%d):\n\n' "$rel" "$line_num" "$start" "$end"
            printf '```%s\n' "$lang"
            sed -n "${start},${end}p" "$resolved"
            printf '\n```\n\n'
        else
            embed_file "$resolved" "$rel"
        fi
    done
fi
