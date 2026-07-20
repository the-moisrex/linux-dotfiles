#!/usr/bin/env bash
set -euo pipefail

exclude_patterns=()
strip_lists=false
head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt cmake [--head N] [--exclude PATTERN] [--strip-lists]

Gathers all CMakeLists.txt and *.cmake files in the current project to provide full context on the CMake build setup.

Options:
  --head N          Keep only the first N lines of each file
  --exclude PATTERN Exclude files or directories matching the pattern (can be specified multiple times)
  --strip-lists     Replace long lists of source files (.c, .cpp, .h, etc.) with a comment to save context window space
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

# Parse custom arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --exclude)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --exclude" >&2
                exit 2
            fi
            exclude_patterns+=("$2")
            shift 2
        ;;
        --strip-lists)
            strip_lists=true
            shift
        ;;
        *)
            shift
        ;;
    esac
done

echo "Here is the complete CMake configuration for the current project."
echo "Please review the build setup, dependencies, and target structures."
echo

find_git_root

if [[ -n "${GIT_ROOT:-}" ]]; then
    # Search tracked files if in a git repo
    mapfile -t all_files < <(cd "$GIT_ROOT" && git ls-files '*CMakeLists.txt' '*.cmake')
    search_dir="$GIT_ROOT"
else
    # Fallback to standard find
    mapfile -t all_files < <(find . -type f \( -name "CMakeLists.txt" -o -name "*.cmake" \))
    search_dir="."
fi

for f in "${all_files[@]}"; do
    # Remove leading ./ from find output if present
    f="${f#./}"
    
    exclude=false
    for pat in "${exclude_patterns[@]}"; do
        if [[ "$f" == *"$pat"* ]]; then
            exclude=true
            break
        fi
    done
    
    if $exclude; then
        continue
    fi
    
    file_path="$search_dir/$f"
    
    if [[ ! -f "$file_path" ]]; then
        continue
    fi
    
    echo "File: $f"
    echo '
    ```cmake'
    
    content="$(cat -- "$file_path")"
    
    if $strip_lists; then
        # Replace contiguous lines containing purely source/header file names with a single omission comment
        content=$(echo "$content" | awk '
/^[[:space:]]*[a-zA-Z0-9_./-]+\.(c|cpp|cc|cxx|h|hpp|hh|ixx)[[:space:]]*$/ {
if (!in_list) {
print "    # ... source file list omitted ..."
in_list = 1
}
next
}
{
in_list = 0
print $0
}
        ')
    fi
    
    trim_context "$content"
    
    echo '
    ```'
    echo
done
