#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt clang-tidy [--head N] [FILE...]
       some-command | prompt clang-tidy [--head N] [FILE...]

Runs `clang-tidy` on the provided files and outputs a prompt to fix the identified issues.
If no files are provided, `fzf -m` is used to choose them interactively.

Options:
  --head N   Keep only the first N lines of the output
  -h, --help Show this help
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

if git rev-parse --show-toplevel >/dev/null 2>&1; then
    GIT_ROOT="$(git rev-parse --show-toplevel)"
else
    GIT_ROOT=""
fi

if [ $# -eq 0 ]; then
    # Using an array to safely capture the output
    mapfile -t selected_files < <(select_files)
    set -- "${selected_files[@]}"
fi

if [ $# -eq 0 ]; then
    printf 'No files specified or selected.\n' >&2
    exit 0
fi

find_build_dir() {
    local search_bases=("$PWD")
    [[ -n "$GIT_ROOT" ]] && search_bases+=("$GIT_ROOT")
    
    local common_dirs=("build" "build-dev" "build-dev-clang" "out" "build/Debug" "build/Release" ".")
    
    for base in "${search_bases[@]}"; do
        for dir in "${common_dirs[@]}"; do
            local check_path="$base/$dir"
            if [[ -f "$check_path/compile_commands.json" ]]; then
                # Return canonicalized path
                realpath "$check_path"
                return 0
            fi
        done
    done
    return 1
}

find_clang_tidy_config() {
    local curr="$PWD"
    while [[ -n "$curr" && "$curr" != "/" ]]; do
        if [[ -f "$curr/.clang-tidy" ]]; then
            realpath "$curr/.clang-tidy"
            return 0
        fi
        [[ -n "$GIT_ROOT" && "$curr" == "$GIT_ROOT" ]] && break
        curr="$(dirname "$curr")"
    done
    return 1
}

BUILD_DIR="$(find_build_dir || true)"
CONFIG_FILE="$(find_clang_tidy_config || true)"

CT_ARGS=()
if [[ -n "$CONFIG_FILE" ]]; then
    CT_ARGS+=("--config-file=$CONFIG_FILE")
fi
if [[ -n "$BUILD_DIR" ]]; then
    CT_ARGS+=("-p" "$BUILD_DIR")
fi

tmp_output="$(mktemp)"
# Run clang-tidy
clang-tidy "${CT_ARGS[@]}" "$@" > "$tmp_output" 2>&1 || true

run_output="$(cat "$tmp_output")"
rm -f "$tmp_output"

if [[ -n "$head_lines" ]]; then
    run_output="$(printf "%s" "$run_output" | head -n "$head_lines")"
fi

echo "I ran \`clang-tidy\` on the following files:"
for f in "$@"; do
    echo "- \`$f\`"
done

echo ""
echo "Please review the following \`clang-tidy\` output. Identify the issues, explain why they were flagged, and provide the most robust and idiomatic fixes. Suggest small git patches or refactored code blocks where appropriate."
echo ""
echo '
```text'
printf "%s\n" "$run_output"
echo '
```'
