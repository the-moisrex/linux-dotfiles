#!/usr/bin/env bash
set -euo pipefail

prompt=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-output.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "test_output: $*" >&2
    exit 1
}

# A segment is trimmed on both sides: leading and trailing blank lines never
# reach stdout (the clipboard and the chain context already worked this way).
"$prompt" .note $'\n\nX\n\n\n' </dev/null >"$tmp/trim.out" 2>/dev/null
printf 'X\n' >"$tmp/trim.want"
cmp -s "$tmp/trim.out" "$tmp/trim.want" || fail "a segment was not trimmed on both sides"

# Consecutive segments fold with exactly one blank line and a single trailing
# newline, wherever their own blank edges were.
"$prompt" .note a .note $'\nb\n' </dev/null >"$tmp/pair.out" 2>/dev/null
printf 'a\n\nb\n' >"$tmp/pair.want"
cmp -s "$tmp/pair.out" "$tmp/pair.want" || fail "two segments did not fold with one blank line"

# A segment that emits nothing contributes nothing: no stray separator.
"$prompt" .note a .note "" .note b </dev/null >"$tmp/empty.out" 2>/dev/null
cmp -s "$tmp/empty.out" "$tmp/pair.want" || fail "an empty segment added a separator"

# The recorded replay uses the same rule, so `.history` is byte-identical.
export PROMPT_HISTORY_DIR="$tmp/store"
"$prompt" .note $'\n\nhello\n\n' </dev/null >"$tmp/run.out" 2>/dev/null
"$prompt" .history >"$tmp/hist.out" 2>/dev/null
cmp -s "$tmp/run.out" "$tmp/hist.out" || fail ".history does not reproduce the trimmed stdout"

echo "all output tests passed"
