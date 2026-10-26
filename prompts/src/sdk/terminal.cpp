#include "terminal.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <format>
#include <string>
#include <unistd.h>

namespace prompt::terminal {

namespace {

std::string env_value(char const* name) {
    char const* raw = std::getenv(name);
    return raw == nullptr ? std::string{} : std::string(raw);
}

long long sid_from_env() {
    std::string const raw = env_value("PROMPT_TERMINAL_SID");
    if (raw.empty()) return 0;
    try {
        return std::stoll(raw);
    } catch (...) { // NOLINT(bugprone-empty-catch) — a bad override falls back to the system value
        return 0;
    }
}

// The controlling terminal, if the process has one. `/dev/tty` is authoritative
// even when stdin/stdout are pipes (the history recorder also runs in forked
// children whose stdio is redirected), so it is tried first; the standard file
// descriptors are the fallback for setups where `/dev/tty` is unavailable but
// one of them is still a terminal.
std::string controlling_tty() {
    auto name_of = [](int fd) {
        std::array<char, 256> buffer{};
        if (::ttyname_r(fd, buffer.data(), buffer.size()) != 0) return std::string{};
        return std::string(buffer.data());
    };
    int const fd = ::open("/dev/tty", O_RDONLY | O_CLOEXEC | O_NOCTTY);
    if (fd >= 0) {
        std::string const tty = name_of(fd);
        ::close(fd);
        if (!tty.empty()) return tty;
    }
    for (int const candidate : std::array{STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        if (auto tty = name_of(candidate); !tty.empty()) return tty;
    }
    return {};
}

} // namespace

id current() {
    id value;
    value.key = env_value("PROMPT_TERMINAL_KEY");
    if (!value.key.empty()) return value;

    value.sid = sid_from_env();
    if (value.sid == 0) value.sid = static_cast<long long>(::getsid(0));
    value.sid = std::max(value.sid, 0LL);

    value.tty = env_value("PROMPT_TERMINAL_TTY");
    if (value.tty.empty()) value.tty = controlling_tty();
    return value;
}

bool same(id const& a, id const& b) {
    if (a.empty() || b.empty()) return false;
    // An opaque key replaces the system identity: both sides must carry the
    // same one (a half-set key is not comparable to a system identity).
    if (!a.key.empty() || !b.key.empty()) return !a.key.empty() && a.key == b.key;
    if (a.sid != 0 && b.sid != 0) {
        if (a.sid != b.sid) return false;
        // A recycled session id must not resurrect another terminal's runs:
        // when both sides know their tty, it has to agree too.
        return a.tty.empty() || b.tty.empty() || a.tty == b.tty;
    }
    // No session id on one side (an older run, an odd sandbox): the tty is all
    // that is left.
    if (!a.tty.empty() && !b.tty.empty()) return a.tty == b.tty;
    return false;
}

std::string describe(id const& value) {
    if (!value.key.empty()) return std::format("session {}", value.key);
    std::string out;
    if (value.sid != 0) out = std::format("sid {}", value.sid);
    if (!value.tty.empty()) {
        if (!out.empty()) out += " on ";
        out += value.tty;
    }
    return out.empty() ? "unknown" : out;
}

} // namespace prompt::terminal
