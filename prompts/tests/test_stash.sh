#!/usr/bin/env bash
# End-to-end `.stash`/`.stashed`: append/replace, scope shadowing, fact and flag
# restoration, the management subcommands and completion. Everything runs
# against a scratch PROMPT_STASH_DIR, so the real store is never touched.
set -euo pipefail

prompt=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-stash.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

export PROMPT_STASH_DIR="$tmp/stashes"
export PROMPT_TERMINAL_KEY=test-terminal
export PROMPT_HISTORY=0

fail() {
    echo "test_stash: $*" >&2
    exit 1
}

# --- basic save and append -------------------------------------------------
"$prompt" .note alpha .stash >/dev/null 2>"$tmp/a.err"
grep -q 'prompt stash: wrote' "$tmp/a.err" || fail "first stash status"
"$prompt" .note beta .stash >/dev/null 2>"$tmp/b.err"
grep -q 'appended to' "$tmp/b.err" || fail "second stash appended"

restored=$("$prompt" .stashed 2>/dev/null)
grep -qx alpha <<<"$restored" || fail "restored text lacks alpha"
grep -qx beta <<<"$restored" || fail "restored text lacks beta"

# --- --quiet suppresses the status line -----------------------------------
err=$("$prompt" .note gamma .stash --quiet 2>&1 >/dev/null)
[ -z "$err" ] || fail "--quiet still printed: $err"

# --- --replace and --no-clobber -------------------------------------------
"$prompt" .note replaced .stash --replace >/dev/null 2>&1
out=$("$prompt" .stashed 2>/dev/null)
grep -qx replaced <<<"$out" || fail "--replace did not overwrite"
if grep -qx alpha <<<"$out"; then fail "--replace kept the old text"; fi

if "$prompt" .note x .stash --no-clobber >/dev/null 2>"$tmp/nc.err"; then
    fail "--no-clobber accepted an existing stash"
fi
grep -q 'already exists' "$tmp/nc.err" || fail "--no-clobber error was unclear"

# --- facts restore ---------------------------------------------------------
printf 'hello\n' >"$tmp/sample.txt"
"$prompt" .note review .files "$tmp/sample.txt" .stash >/dev/null 2>&1
mkdir "$tmp/saved"
"$prompt" .stashed --quiet .save "$tmp/saved" >/dev/null 2>&1
[ -f "$tmp/saved/sample.md" ] || fail "the restored save.stem was not used"

# --- --text-only keeps the current facts ----------------------------------
rm -f "$tmp/saved/sample.md"
printf 'other\n' >"$tmp/other.txt"
"$prompt" .files "$tmp/other.txt" .stashed --text-only --quiet .save "$tmp/saved" >/dev/null 2>&1
[ -f "$tmp/saved/other.md" ] || fail "--text-only did not keep the current save.stem"

# --- multiple names merge in order ----------------------------------------
"$prompt" .note one .stash first >/dev/null 2>&1
"$prompt" .note two .stash second >/dev/null 2>&1
out=$("$prompt" .stashed first second 2>/dev/null)
[ "$(grep -cx one <<<"$out")" = 1 ] || fail "first name missing"
[ "$(grep -cx two <<<"$out")" = 1 ] || fail "second name missing"
first_line=$(grep -nx one <<<"$out" | cut -d: -f1)
second_line=$(grep -nx two <<<"$out" | cut -d: -f1)
[ "$first_line" -lt "$second_line" ] || fail "names merged out of order"

# --- --merge keeps the current context and appends the stash --------------
rm -f "$tmp/merged.md"
"$prompt" .note current .stashed first --merge --quiet .save "$tmp/merged.md" >/dev/null 2>&1
grep -qx current "$tmp/merged.md" || fail "--merge dropped the current context"
grep -qx one "$tmp/merged.md" || fail "--merge dropped the stash text"

# --- scope shadowing -------------------------------------------------------
export PROMPT_TERMINAL_KEY=test-terminal
"$prompt" .note global-text .stash scoped >/dev/null 2>&1
"$prompt" .note here-text .stash scoped --here >/dev/null 2>&1
out=$("$prompt" .stashed scoped 2>/dev/null)
grep -qx here-text <<<"$out" || fail "--here did not shadow the global stash"
out=$("$prompt" .stashed --global scoped 2>/dev/null)
grep -qx global-text <<<"$out" || fail "--global did not read the global stash"

listing=$("$prompt" stash.list 2>/dev/null)
grep -q 'scoped' <<<"$listing" || fail "stash.list lacks the name"
grep -q 'here' <<<"$listing" || fail "stash.list lacks the terminal scope"

# A different terminal key sees only the global copy.
out=$(PROMPT_TERMINAL_KEY=other-terminal "$prompt" .stashed scoped 2>/dev/null)
grep -qx global-text <<<"$out" || fail "another terminal read the terminal stash"

# --- stash.show ------------------------------------------------------------
out=$("$prompt" stash.show first 2>/dev/null)
grep -qx one <<<"$out" || fail "stash.show did not print the context"
out=$("$prompt" stash.show first --facts 2>/dev/null)
grep -q 'Facts:' <<<"$out" || fail "stash.show --facts lacks the section"

# --- stash.list --json is valid JSON --------------------------------------
if command -v python3 >/dev/null 2>&1; then
    "$prompt" stash.list --json 2>/dev/null | python3 -c 'import json,sys; json.load(sys.stdin)' ||
        fail "stash.list --json is not valid JSON"
fi

# --- completion lists names ------------------------------------------------
out=$("$prompt" stashed --complete "" 2>/dev/null)
grep -q '^scoped' <<<"$out" || fail "completion lacks a saved name"

# --- drop ------------------------------------------------------------------
"$prompt" stash.drop first >/dev/null 2>&1
if "$prompt" .stashed first >/dev/null 2>&1; then fail "drop left the stash behind"; fi

# Dropping a missing stash is a failure.
if "$prompt" stash.drop missing >/dev/null 2>&1; then fail "drop of a missing stash succeeded"; fi

# --all NAME removes both scopes.
"$prompt" stash.drop --all scoped >/dev/null 2>&1
if "$prompt" .stashed --global scoped >/dev/null 2>&1; then fail "--all left the global copy"; fi
if "$prompt" .stashed scoped >/dev/null 2>&1; then fail "--all left the terminal copy"; fi

# --dry-run removes nothing.
"$prompt" .note keep .stash keepme >/dev/null 2>&1
"$prompt" stash.drop --dry-run keepme >/dev/null 2>&1
out=$("$prompt" .stashed keepme 2>/dev/null)
grep -qx keep <<<"$out" || fail "--dry-run removed the stash"

# --- redaction survives the round trip ------------------------------------
"$prompt" .note "contact user@example.com" .stash mail >/dev/null 2>&1
rm -f "$tmp/mail.md"
"$prompt" .stashed mail --quiet .save "$tmp/mail.md" >/dev/null 2>&1
grep -q 'EMAIL_ADDRESS' "$tmp/mail.md" || fail "the redacted placeholder was lost"
grep -q 'redacted by the prompt pipeline' "$tmp/mail.md" || fail "the redaction legend was lost"

# --- malformed files are reported, not fatal ------------------------------
mkdir -p "$PROMPT_STASH_DIR"
printf '{not json' >"$PROMPT_STASH_DIR/broken.json"
if "$prompt" .stashed broken >/dev/null 2>"$tmp/broken.err"; then
    fail "a malformed stash loaded"
fi
grep -q 'not a valid stash file' "$tmp/broken.err" || fail "malformed error was unclear"

# --- empty stash is saved silently and restores empty ---------------------
"$prompt" .stash empty-stash --quiet >/dev/null 2>&1 || fail "empty stash failed"
out=$("$prompt" .stashed empty-stash 2>/dev/null)
[ -z "$out" ] || fail "an empty stash restored text"

echo "test_stash: ok"
