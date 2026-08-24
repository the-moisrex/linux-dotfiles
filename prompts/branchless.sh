#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt branchless [--head N] [FILE...]
       some-command | prompt branchless [--head N] [FILE...]

Rewrite this C++ code to be branchless.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


cat <<'PROMPT'
Rewrite this C++ code to be branchless. Eliminate all conditional branches (if, else, switch, early returns in hot loops) that harm branch prediction.

Use these techniques — each in the shortest sentence:

BRANCHLESS TECHNIQUES:
- Ternary (?:) often compiles to CMOV (conditional move) — no pipeline flush.
- Bitmask from comparison: mask = (a > b) - 1; gives 0x00000000 or 0xFFFFFFFF.
- Bool as integer: bools are 0 or 1 in arithmetic. x += (int)flag;
- Bool multiplication: result = flag * value; selects value or 0.
- Sign-bit right-shift: mask = x >> 31; gives all 0s or all 1s.
- Branchless abs: mask = x >> 31; abs = (x ^ mask) - mask;
- XOR swap without temp: x ^= y; y ^= x; x ^= y;
- Lookup table: replace complex conditions with table[condition].
- std::min / std::max / std::clamp often compile to CMOV — prefer them over if-else.
- Evaluate both paths, select one: result = (c * a) + (!c * b);
- Bitwise select: result = (a & mask) | (b & ~mask); mask from comparison.
- Boolean logic to mask: mask = -(int)(bool_expr); negation gives all 1s or 0s.

RULES:
- Only optimize branches that are genuinely unpredictable (random data, user input).
- Do NOT touch branches that are highly predictable — they are faster as branches.
- Preserve correctness. The branchless version must produce identical results.
- Prefer readability when the technique is obvious (std::min, ternary).
- Use bitwise tricks only when the benefit is clear.
- Keep the diff minimal — change only what needs to be branchless.

OUTPUT:
- Briefly explain which branches you removed and why.
- Provide a git diff for the changes.
PROMPT

echo

for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo '```'
        echo
    else
        echo "Warning: File not found or is not a regular file: $file" >&2
    fi
done
