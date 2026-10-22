#!/usr/bin/env bash
set -euo pipefail

prompt=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-clear.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "test_clear: $*" >&2
    exit 1
}

# .clear resets the context: a later .save writes only the new section.
"$prompt" .note FIRST .clear .note SECOND .save "$tmp/section.md" >/dev/null 2>&1
grep -qx 'SECOND' "$tmp/section.md" || fail "post-clear context is not a fresh section"
if grep -q 'FIRST' "$tmp/section.md"; then
    fail ".clear left the previous context in place"
fi

# It only resets the chain context: output already emitted is still printed.
"$prompt" .note VISIBLE .clear --show-context .note AFTER .save "$tmp/kept.md" >"$tmp/kept.out" 2>/dev/null
grep -qx 'AFTER' "$tmp/kept.md" || fail "clear + show-context save content"
grep -q 'VISIBLE' "$tmp/kept.out" || fail ".clear hid already-emitted output"

# Piped stdin is part of the context, so a leading .clear discards it.
printf 'piped\n' | "$prompt" .clear .note kept .save "$tmp/stdin.md" >/dev/null 2>&1
grep -qx 'kept' "$tmp/stdin.md" || fail "save after clear content"
if grep -q 'piped' "$tmp/stdin.md"; then
    fail ".clear kept piped stdin"
fi

# .clear rejects arguments (exit 2) instead of silently ignoring them.
set +e
"$prompt" .clear extra >/dev/null 2>"$tmp/args.err"
status=$?
set -e
[ "$status" -eq 2 ] || fail ".clear should exit 2 on arguments (got $status)"
grep -q 'does not take arguments' "$tmp/args.err" || fail ".clear argument error"

# The filename hint is part of the context; .clear drops it.
printf 'sample\n' >"$tmp/sample.txt"
mkdir "$tmp/first" "$tmp/second"
"$prompt" .note a .files "$tmp/sample.txt" .save "$tmp/first" .clear .note b .save "$tmp/second" >/dev/null 2>&1
[ -f "$tmp/first/sample.md" ] || fail "first section did not use the .files hint"
count=$(find "$tmp/second" -maxdepth 1 -name 'prompt-*.md' | wc -l)
[ "$count" -eq 1 ] || fail ".clear kept the old filename hint"

# A failed section's taint is reset too, so the next section can run sinks.
set +e
"$prompt" .save --on-error continue .clear --on-error continue .note b .save "$tmp/after.md" >/dev/null 2>"$tmp/taint.err"
status=$?
set -e
[ "$status" -ne 0 ] || fail "the intentionally failing save did not fail"
[ -f "$tmp/after.md" ] || fail ".clear did not reset the source-batch failure taint"

# .flush is .save + .clear.
"$prompt" .note flushed .flush "$tmp/flush.md" .note fresh .save "$tmp/fresh.md" >/dev/null 2>"$tmp/flush.err"
grep -qx 'flushed' "$tmp/flush.md" || fail ".flush did not save the context"
grep -qx 'fresh' "$tmp/fresh.md" || fail ".flush did not clear the context"
grep -q 'prompt flush: wrote' "$tmp/flush.err" || fail ".flush status on stderr"

# Both spellings produce the same file.
"$prompt" .note same .flush "$tmp/flush-compare.md" >/dev/null 2>&1
"$prompt" .note same .save "$tmp/save-then-clear.md" .clear >/dev/null 2>&1
cmp -s "$tmp/flush-compare.md" "$tmp/save-then-clear.md" || fail ".flush differs from .save + .clear"

# .flush reuses .save's no-clobber and --force rules.
if "$prompt" .note other .flush "$tmp/flush.md" >/dev/null 2>&1; then
    fail ".flush overwrote without --force"
fi
"$prompt" .note other .flush "$tmp/flush.md" --force >/dev/null 2>&1
grep -qx 'other' "$tmp/flush.md" || fail ".flush --force did not overwrite"

# .flush preflights its destination before any prompt runs.
printf 'not a directory\n' >"$tmp/notdir"
if "$prompt" .cli "touch $tmp/marker" .flush --dir "$tmp/notdir" >/dev/null 2>"$tmp/notdir.err"; then
    fail ".flush accepted a non-directory target"
fi
grep -q 'not a directory' "$tmp/notdir.err" || fail ".flush preflight error was unclear"
[ ! -e "$tmp/marker" ] || fail ".flush preflight ran after the preceding prompt"

# Help documents both sinks and completion finds .flush's save flags.
"$prompt" .clear --help | grep -q 'independent section' || fail ".clear help"
"$prompt" .flush --help | grep -q -- '--allow-empty' || fail ".flush help omits --allow-empty"
"$prompt" flush --complete -- | grep -q -- '--force' || fail ".flush completion omits --force"

echo "all clear/flush tests passed"
