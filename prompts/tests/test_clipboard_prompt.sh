#!/usr/bin/env bash
set -euo pipefail

prompt=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-clipboard.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "test_clipboard_prompt: $*" >&2
    exit 1
}

# The native clipboard backend is the session's own tool; stub xclip so the test
# is deterministic and headless. `-i` (copy) buffers stdin, `-o` (paste) prints
# it, exactly as bin/clipboard drives xclip.
mkdir "$tmp/bin"
cat >"$tmp/bin/xclip" <<'EOF'
#!/usr/bin/env bash
store=${CLIP_TEST_STORE:-}
mode=
for arg in "$@"; do
    case "$arg" in
        -o) mode=read ;;
        -i) mode=write ;;
    esac
done
case "$mode" in
    read) [ -n "$store" ] && cat "$store" 2>/dev/null; exit 0 ;;
    write) [ -n "$store" ] && cat >"$store"; exit 0 ;;
    *) exit 0 ;;
esac
EOF
chmod +x "$tmp/bin/xclip"

store="$tmp/clipboard"
export CLIP_TEST_STORE="$store"
export PATH="$tmp/bin:$PATH"
export DISPLAY=:0
unset WAYLAND_DISPLAY KDE_SESSION_VERSION

set_clipboard() { printf '%s' "$1" >"$store"; }

# `.clipboard` pastes; the optional `paste` subcommand and the `.c.p` alias do
# the same, and `--head N` caps the pasted text.
set_clipboard $'PASTED\nline2\nline3\n'
"$prompt" .clipboard >"$tmp/plain.out" 2>/dev/null
grep -qx 'PASTED' "$tmp/plain.out" || fail ".clipboard did not paste"
grep -qx 'line3' "$tmp/plain.out" || fail ".clipboard dropped lines"

set_clipboard $'PASTED\nline2\nline3\n'
"$prompt" .clipboard paste >"$tmp/sub.out" 2>/dev/null
grep -qx 'PASTED' "$tmp/sub.out" || fail ".clipboard paste did not paste"

set_clipboard $'PASTED\nline2\nline3\n'
"$prompt" .c.p >"$tmp/cp.out" 2>/dev/null
grep -qx 'PASTED' "$tmp/cp.out" || fail ".c.p did not paste"

set_clipboard $'PASTED\nline2\nline3\n'
"$prompt" .clipboard paste --head 1 >"$tmp/head.out" 2>/dev/null
grep -qx 'PASTED' "$tmp/head.out" || fail ".clipboard --head kept no first line"
if grep -q 'line2' "$tmp/head.out"; then
    fail ".clipboard --head did not cap the pasted text"
fi

# Blank edges a terminal copy carries are trimmed on both sides, and `--head N`
# counts from the first real line.
set_clipboard $'\n\nPASTED\nline2\n\n'
"$prompt" .c.p >"$tmp/lead.out" 2>/dev/null
printf 'PASTED\nline2\n' >"$tmp/lead.want"
cmp -s "$tmp/lead.out" "$tmp/lead.want" || fail ".c.p kept a blank edge"
"$prompt" .c.p --head 1 >"$tmp/lead-head.out" 2>/dev/null
printf 'PASTED\n' >"$tmp/lead-head.want"
cmp -s "$tmp/lead-head.out" "$tmp/lead-head.want" || fail ".c.p --head 1 counted a blank edge as a line"

# `.c.c` copies the accumulated chain context to the clipboard...
set_clipboard 'SENTINEL'
"$prompt" .note COPYME .c.c >/dev/null 2>&1
grep -qx 'COPYME' "$store" || fail ".c.c did not copy the chain context"

# ...caps it with the dispatcher-owned --head...
set_clipboard 'SENTINEL'
"$prompt" .note one .note two .c.c --head 1 >/dev/null 2>&1
grep -qx 'one' "$store" || fail ".c.c --head did not cap the context"
if grep -q 'two' "$store"; then
    fail ".c.c --head left the tail in place"
fi

# ...and is a passthrough: a later sink still sees the same context.
"$prompt" .note KEEPME .c.c .save "$tmp/keep.md" >/dev/null 2>&1
grep -qx 'KEEPME' "$tmp/keep.md" || fail ".c.c did not pass the context through"

# The copy hint: `.clipboard copy` cannot run as a source and points at `.c.c`.
set +e
"$prompt" .clipboard copy >/dev/null 2>"$tmp/copy.err"
status=$?
set -e
[ "$status" -eq 2 ] || fail ".clipboard copy should exit 2 (got $status)"
grep -q '\.c\.c' "$tmp/copy.err" || fail ".clipboard copy did not point at .c.c"

# An empty context and stray arguments are errors, not silent no-ops.
set +e
"$prompt" .c.c >/dev/null 2>"$tmp/empty.err"
status=$?
set -e
[ "$status" -eq 1 ] || fail ".c.c should exit 1 on an empty context (got $status)"
grep -q 'no chain context' "$tmp/empty.err" || fail ".c.c empty-context message"

set +e
"$prompt" .note x .c.c extra >/dev/null 2>"$tmp/args.err"
status=$?
set -e
[ "$status" -eq 2 ] || fail ".c.c should exit 2 on arguments (got $status)"
grep -q 'does not take arguments' "$tmp/args.err" || fail ".c.c argument error"

# Help and completion expose the new names and the sink flags.
"$prompt" .c.c --help | grep -q 'Copy the accumulated chain context' || fail ".c.c help"
"$prompt" .c.p --help | grep -q 'Paste alias' || fail ".c.p help"
"$prompt" .clipboard --help | grep -q '\.c\.c' || fail ".clipboard help omits .c.c"
"$prompt" c.c --complete -- | grep -q -- '--with-context' || fail ".c.c completion omits sink flags"
"$prompt" c.p --complete -- | grep -q -- '--head' || fail ".c.p completion omits --head"

echo "all clipboard prompt tests passed"
