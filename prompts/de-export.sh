#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt de-export [--head N] [FILE...]
       some-command | prompt de-export [--head N] [FILE...]

Reduce unnecessary exports from C++20 modules and move implementations out of headers.

Only applies de-export advice if the project uses C++20 modules and contains `export`s.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

is_cpp_file() {
    local base ext
    base="$(basename "$1")"
    ext="${base##*.}"
    case "$ext" in
        cpp|hpp|cxx|hxx|cc|hh|cppm|ixx|mpp|ccm) return 0 ;;
    esac
    case "$base" in
        CMakeLists.txt) return 0 ;;
    esac
    return 1
}

has_modules=false
has_exports=false

cpp_files=()
all_content=""

if [[ -n "$stdin_content" ]]; then
    all_content+="$stdin_content"$'\n'
fi

for file in "$@"; do
    if [[ -f "$file" ]] && is_cpp_file "$file"; then
        cpp_files+=("$file")
        all_content+="$(cat -- "$file")"$'\n'
    fi
done

if [[ ${#cpp_files[@]} -eq 0 && -z "$stdin_content" ]]; then
    echo "No C++ files provided. Pass C++ files as arguments or pipe them to stdin." >&2
    exit 1
fi

if echo "$all_content" | grep -qE '^\s*module\s+\S+|^\s*export\s+module\s+\S+|^\s*import\s+\S+|module\s*;'; then
    has_modules=true
fi

if echo "$all_content" | grep -qE '^\s*export\s+\S|^\s*export\s*\{'; then
    has_exports=true
    has_modules=true
fi

echo "You are an expert C++ developer specializing in C++20 modules, build hygiene, and header design."
echo ""

if $has_modules && $has_exports; then
    echo "The code below uses C++20 modules and contains \`export\` declarations."
    echo ""
    echo "- De-exporting:"
    echo "   - Analyze each \`export\` declaration and determine whether the exported entity is actually consumed by external translation units."
    echo "   - Internal helpers, implementation details, and symbols only used within the module should have \`export\` removed."
    echo "   - When de-exporting, also move the declaration to a non-exported partition or to the implementation unit if it is not part of the public API."
    echo "   - Preserve \`export\` on the module interface unit itself and on anything that truly forms the public API."
    echo ""
elif $has_modules; then
    # echo "The code uses C++20 modules but no \`export\` keywords were found in the provided context."
    echo ""
else
    # echo "C++20 module usage was not detected in the provided context."
    # echo "Only provide advice that is relevant to the code shown."
    echo ""
fi

echo "- Moving implementations out of headers:"
echo "   - If an inline function, template specialization, or non-template function body is defined in a header (.h/.hpp) that is also a module interface (.cppm/.ixx), move the body to the corresponding implementation unit (.cpp)."
echo "   - Keep only declarations in the interface when possible; prefer \`export\` on declarations, not definitions."
echo "   - For templates that must remain visible, keep them in the interface but mark implementation details as internal (non-exported)."
echo "   - If a header is not part of a module, prefer forward declarations and moving definitions to .cpp files where feasible."
# echo ""
# echo "Provide a git diff for the recommended changes. Briefly explain each change."
echo

for file in "${cpp_files[@]}"; do
    file_name="$(basename "$file")"
    echo "File: $file_name"
    echo
    echo "\`\`\`$(infer_lang "$file_name")"
    trim_context "$(cat -- "$file")"
    echo
    echo '```'
    echo
done
