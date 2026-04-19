#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt gtest [--head N] [FILE]...
       some-command | prompt gtest [--head N] [FILE]...

Ask the AI to write Google Test (gtest) unit tests for the provided code.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Write comprehensive unit tests for the following C++ code using Google Test (gtest)."
echo "This code is part of a C++ web framework named web++."
echo "Focus on:"
echo "- Clear and descriptive test names."
echo "- Proper Setup/Teardown (using TEST_F and fixtures if necessary)."
echo "- Edge cases and typical web framework scenarios (e.g., malformed inputs, boundaries)."
echo "- Standard gtest macros (EXPECT_EQ, ASSERT_TRUE, EXPECT_THROW, etc.)."
echo "Provide the complete test code implementation."
echo

while [[ $# -gt 0 ]]; do
    file="$1"
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        printf '\n%s' '```'
        echo
    else
        echo "Warning: File not found or not a regular file: $file" >&2
    fi
    shift
done
