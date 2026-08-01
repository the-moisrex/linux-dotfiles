#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt cpp [--head N] [FILE...]
       make 2>&1 | prompt cpp [--head N] [FILE...]

Builds a prompt for generating or editing C++ code.
If C++ compiler errors are detected on stdin, it automatically simplifies the prompt to focus on fixing the compilation issues.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

# Re-usable function to check if a given string looks like C++ compiler/linker output
is_compiler_output() {
    local content="$1"
    # Matches GCC/Clang standard error formats (with or without column numbers),
    # include paths, common linker errors, and build system failures (Ninja/Make).
    if echo "$content" | grep -qiE \
    -e ':[0-9]+:([0-9]+:)? (error|warning|fatal error|note):' \
    -e 'In file included from' \
    -e 'undefined reference to' \
    -e 'no matching function for call to' \
    -e 'ld: symbol\(s\) not found' \
    -e 'FAILED:' \
    -e 'ninja: build stopped' \
    -e 'make\[[0-9]+\]: \*\*\*'; then
        return 0
    fi
    return 1
}


NO_FILES=true
source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

# If we captured stdin, inject it at the top of the context just like `print_stdin` would have
if [[ -n "$stdin_content" ]]; then
    if [[ "$stdin_content" != *"\`\`\`"* ]]; then
        echo '```'
        printf '%s\n\n' "$stdin_content"
        echo '```'
    else
        printf '%s\n\n' "$stdin_content"
    fi
fi

# Dynamically adjust the prompt based on the content of the input
if [[ -n "$stdin_content" ]] && is_compiler_output "$stdin_content"; then
    echo
    echo "The provided output contains C++ compiler errors or warnings."
    echo "Please analyze the error messages and the provided code context."
    echo "Find the root problem and propose the smallest useful fix."
    echo "Explain the issue briefly, and provide the answer primarily as a git diff that can be applied directly."
    echo "Prefer minimal, surgical changes over broad rewrites."
else
    echo "You are an expert C++ developer with deep knowledge of modern C++ (C++17, C++20, C++23, C++26) and strict adherence to the C++ Core Guidelines."
    echo "Follow these principles when providing C++ code:"
    echo ""
    echo "- Use modern C++ features (auto, range-based loops, structured bindings, concepts, modules)"
    echo "- Apply C++ Core Guidelines (ES, SL, NL, F, C, Enum, Con, T, I, R, Pro, E, S, Cp)"
    echo "- Prioritize performance using move semantics, perfect forwarding, and minimal allocations"
    echo "- Minimize dependencies - prefer standard library over third-party libraries"
    echo "- Use constexpr for compile-time computation when possible"
    echo "- Apply [[nodiscard]] to functions whose return values should not be ignored"
    echo "- Prefer RAII and smart pointers for resource management"
    echo "- Leverage templates and concepts for generic programming"
    echo "- Write efficient, safe, readable, and maintainable code"
fi
echo
