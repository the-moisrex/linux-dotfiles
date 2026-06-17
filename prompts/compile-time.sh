#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt fast-compile [--head N] [FILE...]
       some-command | prompt fast-compile [--head N] [FILE...]

Review C++ code for opportunities to reduce compile time.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "Review this C++ code and identify easy fixes to reduce compile time."
echo "Look for:"
echo "- Unnecessary \`#include\` directives that can be removed."
echo "- Opportunities to replace \`#include\` with forward declarations."
echo "- Inline functions or templates in headers that can be moved to source (.cpp) files."
echo "- Template bloat or opportunities for explicit template instantiation."
echo "- Expensive headers (e.g., \`<iostream>\`, \`<regex>\`, \`<windows.h>\`) included in header files instead of source files."
echo "- Moving compile time constants inside a dependent templated out of that dependency."
echo "- Reducing coupling of templated arguments."
echo "- Minimizing template arguments and not packing unnecessary arguments."
echo
echo "Call out which issues matter most in practice, and provide a git diff for the high-impact, easy-to-implement improvements."
echo "Do not sacrifice correctness or introduce severe runtime performance regressions for minor compile-time wins."
echo

for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo '
```'
echo
else
echo "Warning: File not found or is not a regular file: $file" >&2
fi
done

