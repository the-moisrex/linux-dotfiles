#!/usr/bin/env bash
# End-to-end `.ai.debate` sink. A stub provider (no network) proves that:
#   * the accumulated chain text reaches ai.debate on stdin;
#   * the provider names and the debate flags are forwarded untouched;
#   * the styled stream stays display-only and the chain gets the plain answer
#     from --context-file (a following stub sink receives it);
#   * a terminal sink is given --tty plus the real --width/--height;
#   * --print-prompt takes no side channel.
set -euo pipefail

prompt=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-ai-debate.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "test_ai_debate_sink: $*" >&2
    exit 1
}

# The binary is copied beside the stubs: `bin_tool` checks the executable's own
# directory first, so a copy keeps the real providers (network, browser session)
# out of the test.
cp "$prompt" "$tmp/prompt"
prompt=$tmp/prompt

# A stub answers with a "styled" line on stdout and the bare answer through
# --context-file, like bin/ai --tty does: colour only when --tty was passed.
make_stub() {
    local tool=$1 label=$2
    cat >"$tmp/$tool" <<'STUB'
#!/bin/sh
name=@LABEL@
f=""
tty=""
for a in "$@"; do
    case "$a" in
        --context-file=*) f="${a#*=}" ;;
        --tty) tty=1 ;;
    esac
done
input=$(cat)
if [ -n "$tty" ]; then
    printf '\033[1mSTYLED[%s]\033[0m\n' "$input"
else
    printf 'STYLED[%s]\n' "$input"
fi
[ -n "$f" ] && printf '%s[%s]\n' "$name" "$input" >"$f"
printf '%s %s\n' "$(basename "$0")" "$*" >>"$AI_DEBATE_ARGS"
if [ "$(basename "$0")" = "ai.debate" ]; then
    printf '%s\n' "$input" >"$AI_DEBATE_STDIN"
fi
exit 0
STUB
    sed -i "s/@LABEL@/$label/" "$tmp/$tool"
    chmod +x "$tmp/$tool"
}

# `.ai.debate` writes DEBATE[...] to its side channel; a following `.ai.grok`
# sink must receive that (not the styled stream) and render it.
make_stub ai.debate DEBATE
make_stub ai.grok FINAL

run() {
    (cd "$tmp" && ./prompt .note question .ai.debate grok dseek --rounds 1 --new .ai.grok)
}

# Run the chain on a pseudo-terminal of the given size and print its transcript.
run_tty() {
    AI_DEBATE_ARGS="$1" AI_DEBATE_STDIN="$2" python3 - "$prompt" "$tmp" "$3" "$4" <<'PY'
import fcntl
import os
import pty
import struct
import subprocess
import sys
import termios

prompt, tmp, columns, rows = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
proc = subprocess.Popen(
    [prompt, ".note", "question", ".ai.debate", "grok", "dseek", "--rounds", "1",
     ".ai.grok"],
    cwd=tmp, stdin=subprocess.DEVNULL, stdout=slave, stderr=slave)
os.close(slave)
chunks = []
while True:
    try:
        data = os.read(master, 4096)
    except OSError:
        break
    if not data:
        break
    chunks.append(data)
proc.wait()
os.close(master)
sys.stdout.write(b"".join(chunks).decode(errors="replace"))
PY
}

# --- piped: plain context, no terminal flags ----------------------------
export AI_DEBATE_ARGS="$tmp/piped.args"
export AI_DEBATE_STDIN="$tmp/piped.stdin"
piped=$(run 2>/dev/null)
[[ $piped == *'STYLED[DEBATE[question]]'* ]] || fail "chain did not get the context answer: $piped"
[[ $piped != *$'\x1b'* ]] || fail "piped chain emitted ANSI escapes"
[[ $piped != *'STYLED[STYLED'* ]] || fail "chain got the styled stream as context"
grep -q -- '--stdin-only' "$AI_DEBATE_ARGS" || fail "the sink did not ask for stdin-only"
grep -q -- '--context-file=' "$AI_DEBATE_ARGS" || fail "the sink got no --context-file"
grep -q -- 'grok dseek' "$AI_DEBATE_ARGS" || fail "the provider names were not forwarded"
grep -q -- '--rounds 1' "$AI_DEBATE_ARGS" || fail "the debate flags were not forwarded"
grep -q -- '--new' "$AI_DEBATE_ARGS" || fail "the state flag was not forwarded"
grep -q -- '--tty' "$AI_DEBATE_ARGS" && fail "piped sink was given --tty"
grep -qx 'question' "$AI_DEBATE_STDIN" || fail "the question did not reach the tool on stdin"

# --- terminal: --tty and the real size ----------------------------------
tty_out=$(run_tty "$tmp/tty.args" "$tmp/tty.stdin" 120 40)
[[ $tty_out == *$'\x1b'* ]] || fail "terminal sink did not style its output"
[[ $tty_out == *'DEBATE[question]'* ]] || fail "terminal chain did not get the plain answer"
grep -q -- '--tty' "$tmp/tty.args" || fail "terminal sink got no --tty"
grep -q -- '--width=120' "$tmp/tty.args" || fail "terminal sink got no --width"
grep -q -- '--height=40' "$tmp/tty.args" || fail "terminal sink got no --height"

# --- dry runs take no side channel --------------------------------------
AI_DEBATE_ARGS="$tmp/dry.args" \
    bash -c 'cd "$1" && ./prompt .note hi .ai.debate grok dseek --print-prompt' _ "$tmp" >/dev/null 2>&1
grep -q -- '--context-file=' "$tmp/dry.args" && fail "--print-prompt was given --context-file"

echo "test_ai_debate_sink: ok"
