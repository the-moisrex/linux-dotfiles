#!/usr/bin/env bash
# End-to-end multi-value extraction: an unnamed primary plus a `save.stem`
# binding, consumed by `.save`, and the `.delegate`+`.save` filename trigger.
set -euo pipefail

prompt=$(realpath -- "$1")
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-extract-bind.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "test_extract_bind: $*" >&2
    exit 1
}

doc=$'```sh\nprompt .note hi\n```\n\nfilename: fix-parser.prompt\n'

# The primary replaces the context and the binding names the saved file.
"$prompt" .note "$doc" .extract command:prompt save.stem=field:filename \
    .save --dir "$tmp" >/dev/null 2>"$tmp/bind.err"
grep -qx 'prompt .note hi' "$tmp/fix-parser.prompt" || fail "primary content"
grep -q 'prompt save: wrote' "$tmp/bind.err" || fail "save status on stderr"

# The same plan published by `.expect` needs no selector on `.extract`.
rm -f "$tmp/fix-parser.prompt"
"$prompt" .note "$doc" .expect command:prompt save.stem=field:filename \
    .extract .save --dir "$tmp" >/dev/null 2>&1
grep -qx 'prompt .note hi' "$tmp/fix-parser.prompt" || fail "expect-plan binding"

# An explicit target wins over the binding, and `.save` still passes through.
"$prompt" .note "$doc" .extract command:prompt save.stem=field:filename \
    .save "$tmp/explicit.md" >/dev/null 2>&1
grep -qx 'prompt .note hi' "$tmp/explicit.md" || fail "explicit target content"

# A binding with no match fails the whole extraction (nothing is written).
if "$prompt" .note [=[```sh
prompt .note hi
```]=] .extract command:prompt save.stem=field:filename \
    .save --dir "$tmp" >/dev/null 2>"$tmp/missing.err"; then
    fail "a missing binding should fail the extraction"
fi
grep -q 'no block matches' "$tmp/missing.err" || fail "missing-binding message"

# `.delegate` asks for a filename only when a nameless `.save` is in the chain.
if "$prompt" .note x .delegate 2>/dev/null | grep -q 'File name'; then
    fail "delegate asked for a filename without .save"
fi
if "$prompt" .note x .delegate .save "$tmp/explicit2.md" >/dev/null 2>&1 &&
    grep -q 'File name' "$tmp/explicit2.md"; then
    fail "an explicit .save target should not request a filename"
fi
# A nameless `.save` writes into the cwd, so run this case in the scratch dir.
rm -f "$tmp"/prompt-*.md
if ! (cd "$tmp" && "$prompt" .note x .delegate .save >/dev/null 2>&1); then
    fail "delegate + nameless .save failed"
fi
derived=$(find "$tmp" -maxdepth 1 -name 'prompt-*.md' -print -quit)
[ -n "$derived" ] || fail "delegate + derived .save wrote nothing"
grep -q 'File name' "$derived" || fail "delegate did not request a filename"
grep -q 'filename:' "$derived" || fail "delegate did not ask for the filename field"

echo "test_extract_bind: ok"
