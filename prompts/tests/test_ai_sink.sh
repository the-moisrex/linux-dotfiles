#!/usr/bin/env bash
# End-to-end `.ai.*` sink rendering. Stub providers (no network) prove that:
#   * a piped sink stays plain (no ANSI) and never gets --tty;
#   * a sink on a terminal gets --tty plus the real --width/--height;
#   * the styled stream goes to the terminal while the chain gets the plain
#     answer from --context-file.
set -euo pipefail

prompt=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-ai-sink.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "test_ai_sink: $*" >&2
    exit 1
}

# The binary is copied beside the stubs: `bin_tool` checks the executable's own
# directory first, so a copy keeps the real providers (network, browser session)
# out of the test.
cp "$prompt" "$tmp/prompt"
prompt=$tmp/prompt

# The stub answers with a "styled" line on stdout and the bare answer through
# --context-file, like bin/ai --tty does: colour only when --tty was passed.
make_stub() {
    cat >"$tmp/$1" <<'STUB'
#!/bin/sh
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
[ -n "$f" ] && printf 'PLAIN[%s]\n' "$input" >"$f"
printf '%s %s\n' "$(basename "$0")" "$*" >>"$AI_SINK_ARGS"
exit 0
STUB
    chmod +x "$tmp/$1"
}

make_stub ai.dseek
make_stub ai.grok
make_stub ai.lms
make_stub ai.openai

# Everything runs from $tmp so the copied binary's own directory holds the stubs.
run() {
    (cd "$tmp" && ./prompt .note hi .ai.dseek .ai.grok)
}

# Run the chain on a pseudo-terminal of the given size and print its transcript.
run_tty() {
    AI_SINK_ARGS="$1" python3 - "$prompt" "$tmp" "$2" "$3" <<'PY'
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
proc = subprocess.Popen([prompt, ".note", "hi", ".ai.dseek", ".ai.grok"],
                        cwd=tmp, stdin=subprocess.DEVNULL, stdout=slave,
                        stderr=slave)
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

# --- piped: plain output, no terminal flags ------------------------------
export AI_SINK_ARGS="$tmp/piped.args"
piped=$(run 2>/dev/null)
[[ $piped == *'STYLED[PLAIN[hi]]'* ]] || fail "piped chain did not get the plain answer: $piped"
[[ $piped != *$'\x1b'* ]] || fail "piped chain emitted ANSI escapes"
[[ $piped != *'STYLED[STYLED'* ]] || fail "piped chain got the styled stream as context"
grep -q 'ai.dseek .*--tty' "$AI_SINK_ARGS" && fail "piped sink was given --tty"
grep -q -- '--context-file=' "$AI_SINK_ARGS" || fail "piped sink got no --context-file"

# --- terminal: --tty, real size, styled terminal, plain chain -----------
tty_out=$(run_tty "$tmp/tty.args" 120 40)
[[ $tty_out == *$'\x1b'* ]] || fail "terminal sink did not style its output"
[[ $tty_out == *'STYLED[PLAIN[hi]]'* ]] || fail "terminal chain did not get the plain answer"
[[ $tty_out != *'STYLED[STYLED'* ]] || fail "terminal chain got the styled stream as context"
grep -q 'ai.dseek .*--tty' "$tmp/tty.args" || fail "terminal sink got no --tty"
grep -q -- '--width=120' "$tmp/tty.args" || fail "terminal sink got no --width"
grep -q -- '--height=40' "$tmp/tty.args" || fail "terminal sink got no --height"

# --- dry runs take no side channel --------------------------------------
AI_SINK_ARGS="$tmp/dry.args" bash -c 'cd "$1" && ./prompt .note hi .ai.dseek --print-prompt' _ "$tmp" >/dev/null 2>&1
grep -q -- '--context-file=' "$tmp/dry.args" && fail "--print-prompt was given --context-file"

# --- the API providers use the same plumbing ----------------------------
# `.ai.lms` and `.ai.openai` are ordinary sinks: the executor, --tty/_width
# and the --context-file side channel are shared with the web providers.
for provider in ai.lms ai.openai; do
    AI_SINK_ARGS="$tmp/$provider.args" bash -c 'cd "$1" && ./prompt .note hi ".$2"' _ "$tmp" "$provider" >/dev/null
    grep -q -- "--context-file=" "$tmp/$provider.args" || fail ".$provider got no --context-file"
    grep -q -- "--tty" "$tmp/$provider.args" && fail "piped .$provider was given --tty"
done

echo "test_ai_sink: ok"
