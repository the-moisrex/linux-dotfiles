#!/usr/bin/env bash
set -euo pipefail

prompt=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prompt-history.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# Every case writes into a scratch store, never the user's real one.
export PROMPT_HISTORY_DIR="$tmp/store"
runs="$PROMPT_HISTORY_DIR/runs"

fail() {
    echo "test_history: $*" >&2
    exit 1
}

run_count() {
    find "$runs" -maxdepth 1 -name '*.md' 2>/dev/null | wc -l | tr -d ' '
}

# 1. A plain chain is recorded, and `.history` reproduces its stdout exactly.
"$prompt" .note "hello world" >"$tmp/plain.out" 2>/dev/null
[ "$(run_count)" -eq 1 ] || fail "a plain chain did not record exactly one run"
grep -q '<!-- history:run ' "$runs"/*.md || fail "the run marker is missing"
grep -q '<!-- history:segment ' "$runs"/*.md || fail "the segment marker is missing"
grep -q '<!-- history:end ' "$runs"/*.md || fail "the end marker is missing"
grep -q '"\.note"' "$runs"/*.md || fail "the command line was not recorded"

"$prompt" .history >"$tmp/hist.out" 2>/dev/null
cmp -s "$tmp/plain.out" "$tmp/hist.out" || fail ".history does not reproduce stdout"
[ "$(run_count)" -eq 1 ] || fail ".history recorded a run of its own"

# 2. A sink chain round-trips, including the sink's own segment and its input.
"$prompt" .note one .note two .ai.dseek --print-prompt >"$tmp/sink.out" 2>/dev/null
"$prompt" .history >"$tmp/sink-hist.out" 2>/dev/null
cmp -s "$tmp/sink.out" "$tmp/sink-hist.out" || fail ".history does not reproduce a sink chain"

"$prompt" .history --segment ai.dseek >"$tmp/seg.out" 2>/dev/null
cmp -s "$tmp/sink.out" "$tmp/seg.out" || fail "--segment ai.dseek is not the sink output"

"$prompt" .history --input >"$tmp/input.out" 2>/dev/null
grep -q 'one' "$tmp/input.out" || fail "--input lost the barrier context"

"$prompt" .history 1 >"$tmp/rank.out" 2>/dev/null
cmp -s "$tmp/sink-hist.out" "$tmp/rank.out" || fail "rank 1 is not the newest run"

id=$(basename "$(find "$runs" -name '*.md' | sort | tail -1)" .md)
"$prompt" .history "$id" >"$tmp/id.out" 2>/dev/null
cmp -s "$tmp/sink-hist.out" "$tmp/id.out" || fail "an explicit id does not resolve"
"$prompt" .history "${id:0:8}" >"$tmp/prefix.out" 2>/dev/null
cmp -s "$tmp/sink-hist.out" "$tmp/prefix.out" || fail "a date prefix does not resolve"

# 3. `--meta`, `--all` and `--head` (before any later run becomes the newest).
"$prompt" .history --meta >"$tmp/meta.out" 2>/dev/null
grep -q 'argv: ' "$tmp/meta.out" || fail "--meta omits the command line"
grep -q 'segments: 3' "$tmp/meta.out" || fail "--meta omits the segment count"
"$prompt" .history --all >"$tmp/all.out" 2>/dev/null
grep -q '^## ai.dseek' "$tmp/all.out" || fail "--all omits a segment heading"
"$prompt" .history --head 1 >"$tmp/head.out" 2>/dev/null
[ "$(wc -l <"$tmp/head.out" | tr -d ' ')" -eq 1 ] || fail "--head did not cap the output"

# 4. `.history` feeds a sink: the run id becomes the `.save` filename.
mkdir "$tmp/saved"
"$prompt" .history .save --dir "$tmp/saved" >/dev/null 2>&1
[ -f "$tmp/saved/$id.md" ] || fail ".history did not publish the run id as the save name"
grep -q 'two' "$tmp/saved/$id.md" || fail ".save wrote the wrong content"

# 5. `.history.list` and `.history.find`.
"$prompt" .history.list >"$tmp/list.out" 2>/dev/null
grep -q "$id" "$tmp/list.out" || fail ".history.list omits the newest run"
[ "$(wc -l <"$tmp/list.out" | tr -d ' ')" -eq "$(run_count)" ] || fail ".history.list line count"
"$prompt" .history.list --limit 1 >"$tmp/list-one.out" 2>/dev/null
[ "$(wc -l <"$tmp/list-one.out" | tr -d ' ')" -eq 1 ] || fail ".history.list --limit 1"
"$prompt" .history.list --since 1d >/dev/null 2>&1 || fail ".history.list --since rejected"
"$prompt" .history.list --paths >"$tmp/list-paths.out" 2>/dev/null
grep -q '\.md$' "$tmp/list-paths.out" || fail ".history.list --paths"

"$prompt" .history.find two >"$tmp/find.out" 2>/dev/null
grep -q 'ai.dseek' "$tmp/find.out" || fail ".history.find missed a segment body"
grep -q 'argv' "$tmp/find.out" || fail ".history.find missed the command line"
"$prompt" .history.find --name ai.dseek two >"$tmp/find-name.out" 2>/dev/null
grep -q 'ai.dseek' "$tmp/find-name.out" || fail ".history.find --name"
"$prompt" .history.find --regex 'o.e' --case-sensitive >/dev/null 2>&1 || fail ".history.find --regex"
"$prompt" .history.find --context 1 two >"$tmp/find-ctx.out" 2>/dev/null
[ "$(wc -l <"$tmp/find-ctx.out" | tr -d ' ')" -gt 1 ] || fail ".history.find --context"
"$prompt" .history.find --paths two | grep -q '\.md$' || fail ".history.find --paths"
[ "$("$prompt" .history.find --limit 1 two | grep -c '^[0-9]\{8\}-')" -eq 1 ] || fail ".history.find --limit"
set +e
"$prompt" .history.find no-such-text-anywhere >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 1 ] || fail ".history.find should exit 1 with no match (got $status)"

# 6. `PROMPT_HISTORY=0` records nothing.
before=$(run_count)
PROMPT_HISTORY=0 "$prompt" .note ignored >/dev/null 2>&1
[ "$(run_count)" -eq "$before" ] || fail "PROMPT_HISTORY=0 still recorded"

# 7. Inner runs record, tagged with their parent, and can be silenced.
parent=$(basename "$(find "$runs" -name '*.md' | sort | tail -1)" .md)
PROMPT_INNER=1 PROMPT_HISTORY_PARENT="$parent" "$prompt" .note child >/dev/null 2>&1
grep -q "\"parent\":\"$parent\"" "$runs"/*.md || fail "an inner run was not tagged with its parent"
before=$(run_count)
PROMPT_INNER=1 PROMPT_HISTORY_PARENT="$parent" PROMPT_HISTORY_INNER=0 "$prompt" .note quiet >/dev/null 2>&1
[ "$(run_count)" -eq "$before" ] || fail "PROMPT_HISTORY_INNER=0 still recorded"

# `.each` tags its children with the loop run's own id.
"$prompt" .note '| symbol |
| --- |
| a |
| b |' .each .length 10 .end >/dev/null 2>&1
parents=$(grep -ho '"parent":"[^"]*"' "$runs"/*.md | sed 's/.*:"//;s/"$//' | sort -u)
[ -n "$parents" ] || fail ".each did not tag its children"
for parent in $parents; do
    [ -f "$runs/$parent.md" ] || fail "a parent id has no run file: $parent"
done

# 8. `.history.purge`: the dry run reports, `--all --keep 1` prunes, and the run
# being written is never a victim.
"$prompt" .history.purge --dry-run >"$tmp/purge.out" 2>&1
grep -q 'nothing to remove' "$tmp/purge.out" || fail "the default purge had something to remove"
"$prompt" .history.purge --all --keep 1 >/dev/null 2>&1
[ "$(run_count)" -eq 1 ] || fail ".history.purge --all --keep 1 left $(run_count) runs"
"$prompt" .history.purge --all --keep 1 --dry-run >"$tmp/purge-dry.out" 2>&1
[ "$(run_count)" -eq 1 ] || fail "the dry run deleted a run"
"$prompt" .note keepme .history.purge --all >/dev/null 2>&1
grep -q 'keepme' "$runs"/*.md || fail ".history.purge removed the run being written"

# 9. Help and completion.
"$prompt" .history --help | grep -q 'recorded run' || fail ".history help"
"$prompt" .history.list --help | grep -q 'newest first' || fail ".history.list help"
"$prompt" .history.find --help | grep -q -- '--regex' || fail ".history.find help"
"$prompt" .history.purge --help | grep -q -- '--dry-run' || fail ".history.purge help"
newest=$(basename "$(find "$runs" -name '*.md' | sort | tail -1)" .md)
"$prompt" .history --complete "" | grep -qx "$newest" || fail ".history completion omits a run id"
"$prompt" history.find --complete -- | grep -q -- '--context' || fail ".history.find flag completion"
"$prompt" history.find --complete "" --name | grep -qx 'ai.dseek' || fail ".history.find --name completion"
"$prompt" history.purge --complete -- | grep -q -- '--max-size' || fail ".history.purge flag completion"

# 10. Terminal scoping: `.last` and `--here` resolve within this terminal, not
# the whole machine, and a terminal with no runs falls back to the global newest
# with a note. The store holds one run (`keepme`) when this section starts.
term_a() { PROMPT_TERMINAL_SID=1111 PROMPT_TERMINAL_TTY=/dev/pts/11 "$@"; }
term_b() { PROMPT_TERMINAL_SID=2222 PROMPT_TERMINAL_TTY=/dev/pts/22 "$@"; }

term_a "$prompt" .note fromA1 >/dev/null 2>&1
term_b "$prompt" .note fromB1 >/dev/null 2>&1
term_a "$prompt" .note fromA2 >/dev/null 2>&1

term_a "$prompt" .last >"$tmp/last-a.out" 2>/dev/null
grep -q 'fromA2' "$tmp/last-a.out" || fail ".last did not return this terminal's newest run"
if grep -q 'fromB1' "$tmp/last-a.out"; then fail ".last leaked another terminal's run"; fi

term_b "$prompt" .history --here >"$tmp/here-b.out" 2>/dev/null
grep -q 'fromB1' "$tmp/here-b.out" || fail ".history --here did not return this terminal's run"
if grep -q 'fromA2' "$tmp/here-b.out"; then fail ".history --here leaked another terminal's run"; fi

# A rank counts within the terminal, not the global store.
term_a "$prompt" .last 2 >"$tmp/last-a2.out" 2>/dev/null
grep -q 'fromA1' "$tmp/last-a2.out" || fail ".last 2 did not count within the terminal"

# A bare integer is a global rank by default, not a year-prefix id match.
"$prompt" .history 2 >"$tmp/rank2.out" 2>/dev/null
grep -q 'fromB1' "$tmp/rank2.out" || fail ".history 2 is not the global second newest run"

# `--any` reads the globally newest run.
term_a "$prompt" .last --any >"$tmp/last-any.out" 2>/dev/null
grep -q 'fromA2' "$tmp/last-any.out" || fail ".last --any should be the global newest"

# `--meta` names the terminal.
term_a "$prompt" .last --meta >"$tmp/last-meta.out" 2>/dev/null
grep -q 'terminal: sid 1111 on /dev/pts/11' "$tmp/last-meta.out" || fail ".last --meta omitted the terminal"

# A terminal with no runs falls back to the global newest, with a stderr note.
PROMPT_TERMINAL_SID=9999 PROMPT_TERMINAL_TTY=/dev/pts/99 "$prompt" .last >"$tmp/last-fb.out" 2>"$tmp/last-fb.err" ||
    fail ".last fallback failed"
grep -q 'fromA2' "$tmp/last-fb.out" || fail ".last did not fall back to the global newest"
grep -q 'no run from this terminal' "$tmp/last-fb.err" || fail ".last fallback did not explain itself"

# `.history.last` is an alias of `.last`.
term_a "$prompt" .history.last >"$tmp/hlast.out" 2>/dev/null
grep -q 'fromA2' "$tmp/hlast.out" || fail ".history.last is not an alias of .last"

# `--here` on the browsing prompts.
term_a "$prompt" .history.list --here >"$tmp/list-here.out" 2>/dev/null
grep -q 'fromA2' "$tmp/list-here.out" || fail ".history.list --here missed this terminal's runs"
if grep -q 'fromB1' "$tmp/list-here.out"; then fail ".history.list --here leaked another terminal"; fi
term_a "$prompt" .history.find --here fromA2 >/dev/null 2>&1 || fail ".history.find --here did not match this terminal"
if term_b "$prompt" .history.find --here fromA2 >/dev/null 2>&1; then
    fail ".history.find --here matched another terminal's run"
fi

# `--here` and the new `.last` prompts complete and document themselves.
term_a "$prompt" .last --help | grep -q 'this terminal' || fail ".last help"
term_a "$prompt" .last --complete -- | grep -q -- '--any' || fail ".last flag completion"

echo "all history tests passed"
