#!/usr/bin/env bash
set -euo pipefail

prompt=$1
fixture=$2
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-save.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "test_save: $*" >&2
    exit 1
}

# Explicit file target.
"$prompt" .note hello .save "$tmp/explicit.md" >/dev/null 2>"$tmp/explicit.err"
grep -qx 'hello' "$tmp/explicit.md" || fail "explicit file content"
grep -q 'prompt save: wrote' "$tmp/explicit.err" || fail "save status on stderr"

# `--` ends option parsing, so a target may start with a dash.
"$prompt" .note dash .save -- "$tmp/-dash.md" >/dev/null 2>&1
[ -f "$tmp/-dash.md" ] || fail "-- did not allow a dash-prefixed target"

# Refuse to overwrite by default; --force replaces the file.
if "$prompt" .note world .save "$tmp/explicit.md" >/dev/null 2>&1; then
    fail "existing file was overwritten without --force"
fi
"$prompt" .note world .save "$tmp/explicit.md" --force >/dev/null 2>&1
grep -qx 'world' "$tmp/explicit.md" || fail "--force did not overwrite"

# A directory target uses the hint from the nearest successful prompt. The
# `.note` + `.files` batch also exercises the parallel-child hint wire format.
"$prompt" .note hi .files "$fixture" .save "$tmp" >/dev/null 2>&1
[ -f "$tmp/numbered.md" ] || fail "directory target did not use the .files hint"
grep -q 'one' "$tmp/numbered.md" || fail "directory target content"

# No hint: fall back to a timestamped prompt-*.md name.
mkdir "$tmp/nohint"
"$prompt" .note hello .save "$tmp/nohint" >/dev/null 2>&1
count=$(find "$tmp/nohint" -maxdepth 1 -name 'prompt-*.md' | wc -l)
[ "$count" -eq 1 ] || fail "fallback filename"

# Passthrough: .save must leave the AI sink's input unchanged.
"$prompt" .note passthrough .save "$tmp/pass.md" .ai.dseek --print-prompt >"$tmp/ai.out" 2>/dev/null
grep -q 'passthrough' "$tmp/ai.out" || fail "AI sink did not receive the context"
grep -qx 'passthrough' "$tmp/pass.md" || fail "passthrough save content"

# A silent sink is a tap: piped stdin is saved and still reaches a later sink.
printf 'piped\n' | "$prompt" .save "$tmp/stdin.md" .ai.dseek --print-prompt >"$tmp/stdin.ai" 2>/dev/null
grep -qx 'piped' "$tmp/stdin.md" || fail "save did not include piped stdin"
grep -q 'piped' "$tmp/stdin.ai" || fail "save spent the stdin pipe"

# Shared --head trims the saved context.
"$prompt" .note one .note two .save "$tmp/head.md" --head 1 >/dev/null 2>&1
grep -qx 'one' "$tmp/head.md" || fail "--head did not keep the first line"
if grep -q 'two' "$tmp/head.md"; then
    fail "--head left later lines in the file"
fi

# Empty contexts are rejected unless requested.
if "$prompt" .save "$tmp/empty.md" >/dev/null 2>&1; then
    fail "empty context was saved without --allow-empty"
fi
"$prompt" .save "$tmp/empty.md" --allow-empty >/dev/null 2>&1
[ ! -s "$tmp/empty.md" ] || fail "--allow-empty did not write an empty file"

# Hints are sanitized to a safe basename.
printf 'safe\n' >"$tmp/bad name!.txt"
"$prompt" .files "$tmp/bad name!.txt" .save "$tmp" >/dev/null 2>&1
[ -f "$tmp/bad-name.md" ] || fail "unsafe hint was not sanitized"

# Help documents the sink-specific flags and completion finds them.
"$prompt" .save --help | grep -q -- '--allow-empty' || fail "help omits --allow-empty"
"$prompt" save --complete -- | grep -q -- '--force' || fail "completion omits --force"

echo "all save tests passed"
