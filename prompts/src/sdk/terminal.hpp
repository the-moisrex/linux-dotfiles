#pragma once

#include <string>

namespace prompt::terminal {

// A terminal/session identity: what "this terminal" means across separate
// `prompt` invocations, so a feature can scope itself to one place instead of
// the whole machine.
//
// The primary key is the session id (`getsid(0)`): the interactive shell, the
// tmux/kitty pane, or whatever session the invocation belongs to. It is stable
// for the terminal's lifetime, survives a redirected stdin/stdout, and is not
// recycled quickly. The controlling terminal path is recorded alongside as a
// secondary signal (it also guards against a recycled pid/session id). An
// opaque caller-supplied key can replace both.
//
// This is deliberately history-agnostic: the store scopes `.last`/`--here` by
// it, but any future feature that wants "a place, not a global" (a per-terminal
// lock, a session-scoped default, …) can reuse the same identity.
struct id {
    long long sid = 0; // getsid(0); 0 when unknown
    std::string tty;   // controlling terminal path; empty when none
    std::string key;   // opaque override (PROMPT_TERMINAL_KEY); wins over both

    [[nodiscard]] bool empty() const { return sid == 0 && tty.empty() && key.empty(); }
};

// The current process's terminal. System values unless overridden by
// PROMPT_TERMINAL_SID (a decimal session id), PROMPT_TERMINAL_TTY (a path) or
// PROMPT_TERMINAL_KEY (any non-empty opaque string, which wins over both).
id current();

// True when two invocations happened in the same terminal. An opaque key wins
// when both sides have one; otherwise the session ids must match (when both are
// known, the ttys must not disagree); when a session id is missing the tty path
// is compared instead. Two empty identities never match.
bool same(id const& a, id const& b);

// "sid 12345 on /dev/pts/3" / "session <key>" — for `--meta` and `--json`.
std::string describe(id const& value);

} // namespace prompt::terminal
