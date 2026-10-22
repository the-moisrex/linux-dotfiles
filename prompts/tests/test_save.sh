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

# --file forces an exact file name even without an extension.
"$prompt" .note exact .save --file "$tmp/plain" >/dev/null 2>&1
grep -qx 'exact' "$tmp/plain" || fail "--file did not write the exact name"

# --dir forces a directory target, creating it when needed.
"$prompt" .note dir .save --dir "$tmp/forced" >/dev/null 2>&1
[ -d "$tmp/forced" ] || fail "--dir did not create a directory"
grep -rqx 'dir' "$tmp/forced" || fail "--dir content"

# A target with no extension is a directory, created automatically.
"$prompt" .note auto .save "$tmp/auto-dir" >/dev/null 2>&1
[ -d "$tmp/auto-dir" ] || fail "auto target was not created as a directory"
grep -rqx 'auto' "$tmp/auto-dir" || fail "auto directory content"

# Missing parent directories of an exact file target are created too.
"$prompt" .note nested .save "$tmp/a/b/c.md" >/dev/null 2>&1
[ -f "$tmp/a/b/c.md" ] || fail "missing parent directories were not created"
grep -qx 'nested' "$tmp/a/b/c.md" || fail "nested file content"

# The destination is created during preflight, before the sink executes.
"$prompt" .save "$tmp/early" --allow-empty >/dev/null 2>&1
[ -d "$tmp/early" ] || fail "preflight did not create the destination directory"

# Preflight rejects a non-directory path component before any prompt runs.
printf 'not a directory\n' >"$tmp/notdir"
if "$prompt" .cli "touch $tmp/marker" .save "$tmp/notdir/" >"$tmp/notdir.out" 2>"$tmp/notdir.err"; then
    fail "non-directory target was accepted"
fi
grep -q 'not a directory' "$tmp/notdir.err" || fail "non-directory error was unclear"
[ ! -e "$tmp/marker" ] || fail "preflight ran after the preceding prompt"

# Preflight checks write permission before running prompts.
if [ "$(id -u)" -ne 0 ]; then
    mkdir "$tmp/readonly"
    chmod 500 "$tmp/readonly"
    if "$prompt" .note x .save --dir "$tmp/readonly" >/dev/null 2>"$tmp/readonly.err"; then
        chmod 700 "$tmp/readonly"
        fail "read-only target was accepted"
    fi
    chmod 700 "$tmp/readonly"
    grep -q 'cannot write' "$tmp/readonly.err" || fail "read-only error was unclear"
fi

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

# A failed source batch must not make `.save` disappear: the dispatcher names
# the sink it did not run and no file is written. `--days 0` is rejected by the
# tool before it hits the network, so this stays offline and fast.
if "$prompt" .tse khodro --days 0 .save "$tmp/skipped.md" >"$tmp/skipped.out" 2>"$tmp/skipped.err"; then
    fail "a failing source batch was reported as success"
fi
grep -q 'prompt: .save not run: an earlier prompt failed' "$tmp/skipped.err" || fail "skipped save was not reported"
[ ! -e "$tmp/skipped.md" ] || fail "a skipped save still wrote a file"

# The same source batch with --on-error continue reaches `.save`; the empty
# (failed) context is still refused loudly by the save itself.
if "$prompt" .tse khodro --days 0 .save "$tmp/continued.md" --on-error continue >/dev/null 2>"$tmp/continued.err"; then
    fail "an empty continued context was saved"
fi
grep -q 'prompt save: no context to save' "$tmp/continued.err" || fail "empty context error was not reported"
[ ! -e "$tmp/continued.md" ] || fail "an empty context wrote a file"

# Hints are sanitized to a safe basename.
printf 'safe\n' >"$tmp/bad name!.txt"
"$prompt" .files "$tmp/bad name!.txt" .save "$tmp" >/dev/null 2>&1
[ -f "$tmp/bad-name.md" ] || fail "unsafe hint was not sanitized"

# Help documents the sink-specific flags and completion finds them.
"$prompt" .save --help | grep -q -- '--allow-empty' || fail "help omits --allow-empty"
"$prompt" .save --help | grep -q -- '--dir' || fail "help omits --dir"
"$prompt" save --complete -- | grep -q -- '--force' || fail "completion omits --force"
"$prompt" save --complete -- | grep -q -- '--file' || fail "completion omits --file"

echo "all save tests passed"
