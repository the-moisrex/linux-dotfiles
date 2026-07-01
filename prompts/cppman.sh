#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt cppman [--head N] [PAGE...]
       some-command | prompt cppman [--head N] [PAGE...]

Fetches C++ documentation for the specified standard library components using `cppman`
(which pulls from cppreference.com) and appends it to the prompt context.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

if ! command -v cppman >/dev/null 2>&1; then
    echo "Error: cppman is not installed. Please install it (e.g., pip install cppman or your package manager)." >&2
    exit 1
fi

echo "Below is the C++ documentation from cppreference (via cppman) for the requested topics."
echo

# Prevent man/cppman from opening an interactive pager which would hang the script
export PAGER=cat
export MANPAGER=cat

for page in "$@"; do
    echo "Topic: $page"
    echo "\`\`\`text"
    # col -bx strips the overstriking/backspace characters that `man` uses for bold and underline formatting
    if content=$(cppman "$page" 2>/dev/null | col -bx); then
        if [[ -n "$content" ]]; then
            trim_context "$content"
        else
            echo "No documentation content found for '$page'."
        fi
    else
        echo "Failed to fetch documentation for '$page'. (Are you sure it is a valid C++ standard library symbol?)"
    fi
    echo "\`\`\`"
    echo
done
