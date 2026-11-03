#!/usr/bin/env bash
# End-to-end `.ai.reply` (stub providers, no network):
#   * the provider is inferred from this terminal's saved chats;
#   * `-c --reply --terminal-scope` resumes exactly that terminal's chat, not
#     another terminal's;
#   * `--any` falls back to the global chat;
#   * right after an AI sink only the newly appended text is sent;
#   * a missing chat and a debate-last marker fail with a clear message.
set -euo pipefail

prompt=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-ai-reply.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

fail() {
    echo "test_ai_reply: $*" >&2
    exit 1
}

# The binary is copied beside the stubs: `bin_tool` checks the executable's own
# directory first, so a copy keeps the real providers out of the test.
cp "$prompt" "$tmp/prompt"
prompt=$tmp/prompt

# A stub provider that uses the real ai.state: it mirrors the chat under the
# terminal scope, so later `.ai.reply` invocations see it, and it records the
# flags and the resumed conversation id.
make_stub() {
    cat >"$tmp/$1" <<'STUB'
#!/usr/bin/env python3
import json
import os
import sys

sys.path.insert(0, os.environ["AI_TEST_BIN"])
from ai import state

name = os.path.basename(sys.argv[0])
provider = name[3:]
args = sys.argv[1:]
scope = context_file = None
reply = cont = False
for arg in args:
    if arg.startswith("--terminal-scope="):
        scope = arg.split("=", 1)[1]
    elif arg.startswith("--context-file="):
        context_file = arg.split("=", 1)[1]
    elif arg == "--reply":
        reply = True
    elif arg in ("-c", "--continue"):
        cont = True

text = sys.stdin.read()
resume = ""
if cont:
    record = state.load_terminal(provider, scope) if reply else state.load(provider)
    resume = (record or {}).get("conversation_id", "")

sid = os.environ.get("PROMPT_TERMINAL_SID", "")
state.save(provider, f"conv-{provider}-{sid}", "model", scope=scope)
with open(os.environ["AI_TEST_LOG"], "a", encoding="utf-8") as handle:
    handle.write(json.dumps({
        "provider": provider,
        "cont": cont,
        "reply": reply,
        "scope": scope,
        "resume": resume,
        "stdin": text,
    }, ensure_ascii=False) + "\n")
plain = f"answer[{provider}]"
if context_file:
    open(context_file, "w", encoding="utf-8").write(plain)
# bin/ai renders cleanly when piped and styles only on a TTY; the plain answer
# is what the chain and the clipboard take either way.
print(f"styled[{provider}]" if "--tty" in args else plain)
STUB
    chmod +x "$tmp/$1"
}

make_stub ai.gap
make_stub ai.dseek

export AI_TEST_BIN="$root/bin"
export AI_TEST_LOG="$tmp/log"
export XDG_STATE_HOME="$tmp/state"
: >"$AI_TEST_LOG"

term_a() { PROMPT_TERMINAL_SID=1111 PROMPT_TERMINAL_TTY=/dev/pts/11 "$@"; }
term_b() { PROMPT_TERMINAL_SID=2222 PROMPT_TERMINAL_TTY=/dev/pts/22 "$@"; }
term_c() { PROMPT_TERMINAL_SID=7777 PROMPT_TERMINAL_TTY=/dev/pts/77 "$@"; }
term_d() { PROMPT_TERMINAL_SID=9999 PROMPT_TERMINAL_TTY=/dev/pts/99 "$@"; }

# --- two terminals, two providers ---------------------------------------
term_a bash -c 'cd "$1" && ./prompt .note first .ai.gap' _ "$tmp" >/dev/null 2>&1
term_b bash -c 'cd "$1" && ./prompt .note second .ai.dseek' _ "$tmp" >/dev/null 2>&1

out=$(term_a bash -c 'cd "$1" && ./prompt .note "reply a" .ai.reply' _ "$tmp" 2>/dev/null)
[[ $out == *'answer[gap]'* ]] || fail "terminal A reply did not answer: $out"
term_b bash -c 'cd "$1" && ./prompt .note "reply b" .ai.reply' _ "$tmp" >/dev/null 2>&1

# The globally newest chat (terminal B's dseek) is used by --any.
term_c bash -c 'cd "$1" && ./prompt .note global .ai.reply --any' _ "$tmp" >/dev/null 2>&1

# An explicit provider in terminal A must not borrow terminal B's dseek chat.
term_a bash -c 'cd "$1" && ./prompt .note explicit .ai.reply dseek' _ "$tmp" >/dev/null 2>&1

# Same chain: the previous answer is dropped, only the new note is sent.
term_a bash -c 'cd "$1" && ./prompt .note x .ai.gap .note "only this" .ai.reply' _ "$tmp" >/dev/null 2>&1

python3 - "$AI_TEST_LOG" <<'PY' || fail "reply provider/resume selection is wrong"
import json
import sys

records = [json.loads(line) for line in open(sys.argv[1], encoding="utf-8")]
assert [r["provider"] for r in records[:2]] == ["gap", "dseek"], records

a_reply = records[2]
assert a_reply["provider"] == "gap", a_reply
assert a_reply["cont"] and a_reply["reply"], a_reply
assert a_reply["resume"] == "conv-gap-1111", a_reply
assert a_reply["stdin"].strip() == "reply a", a_reply

b_reply = records[3]
assert b_reply["provider"] == "dseek", b_reply
assert b_reply["resume"] == "conv-dseek-2222", b_reply

global_reply = records[4]
assert global_reply["provider"] == "dseek", global_reply
assert global_reply["cont"] and not global_reply["reply"], global_reply
assert global_reply["resume"] == "conv-dseek-2222", global_reply

explicit = records[5]
assert explicit["provider"] == "dseek", explicit
assert explicit["reply"], explicit
assert explicit["resume"] == "", explicit  # no other terminal's chat

same_chain = records[6:]
assert [r["provider"] for r in same_chain] == ["gap", "gap"], same_chain
assert not same_chain[0]["cont"], same_chain[0]
assert same_chain[1]["cont"] and same_chain[1]["reply"], same_chain[1]
assert same_chain[1]["stdin"].strip() == "only this", same_chain[1]
PY

# --- failures ------------------------------------------------------------
set +e
term_d bash -c 'cd "$1" && ./prompt .note hi .ai.reply' _ "$tmp" >/dev/null 2>"$tmp/no-chat.err"
status=$?
set -e
[ "$status" -eq 1 ] || fail "missing chat should exit 1 (got $status)"
grep -q 'no AI chat' "$tmp/no-chat.err" || fail "missing chat message: $(cat "$tmp/no-chat.err")"

term_a bash -c 'cd "$1" && ./prompt .note hi .ai.gap .ai.reply' _ "$tmp" >/dev/null 2>"$tmp/nothing.err" || true
grep -q 'nothing new to send' "$tmp/nothing.err" || fail "empty reply message: $(cat "$tmp/nothing.err")"

mkdir -p "$XDG_STATE_HOME/ai/terminals/sid-9999_tty--dev-pts-99"
echo debate >"$XDG_STATE_HOME/ai/terminals/sid-9999_tty--dev-pts-99/last"
set +e
term_d bash -c 'cd "$1" && ./prompt .note hi .ai.reply' _ "$tmp" >/dev/null 2>"$tmp/debate.err"
status=$?
set -e
[ "$status" -eq 2 ] || fail "debate-last should exit 2 (got $status)"
grep -q 'was a debate' "$tmp/debate.err" || fail "debate message: $(cat "$tmp/debate.err")"

echo "test_ai_reply: ok"