#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt repo [--depth N] [DIR...]

Displays the repository's file structure using only git-tracked files.
Useful for giving an AI a high-level view of the codebase.

Options:
  --depth N   Limit tree depth (default: unlimited)
  --help, -h  Show this help message

Arguments:
  DIR...      Optional directory prefixes to limit the view (e.g., bin/ prompts/)
EOF
}

for arg in "$@"; do
    if [[ "$arg" == "--help" || "$arg" == "-h" ]]; then
        show_help
        exit 0
    fi
done

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "prompt repo: Not inside a git repository." >&2
    exit 1
fi

depth=""
dirs=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --depth)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --depth" >&2
                exit 2
            fi
            depth="$2"
            shift 2
            ;;
        *)
            dirs+=("$1")
            shift
            ;;
    esac
done

echo "# Repository file structure (git-tracked files)"
echo

tree_args=()
if [[ -n "$depth" ]]; then
    tree_args+=("-L" "$depth")
fi

if [[ ${#dirs[@]} -gt 0 ]]; then
    for dir in "${dirs[@]}"; do
        git ls-files -- "$dir" | tree "${tree_args[@]}" --fromfile
    done
else
    git ls-files | tree "${tree_args[@]}" --fromfile
fi
