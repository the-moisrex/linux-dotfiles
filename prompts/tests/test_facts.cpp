#include "../src/sdk/facts.hpp"
#include "../src/sdk/stage.hpp"
#include "../src/sdk/wire.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace prompt;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void check_eq(std::string_view got, std::string_view want, char const* what) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s\n  got:  [%.*s]\n  want: [%.*s]\n", what, static_cast<int>(got.size()),
                     got.data(), static_cast<int>(want.size()), want.data());
        ++failures;
    }
}

// A fact that should be present: a nullopt is a failure in its own right
// ("missing"), never a dereference.
void check_opt(std::optional<std::string_view> const& got, std::string_view want, char const* what) {
    if (!got) {
        std::fprintf(stderr, "FAIL: %s (missing value)\n", what);
        ++failures;
        return;
    }
    check_eq(*got, want, what);
}

void test_basic_get_set() {
    facts f;
    check(f.empty(), "empty facts store reports empty");
    check(!f.get(keys::save_stem), "a missing key is nullopt");

    f.set(keys::save_stem, "tse-foolad");
    check_opt(f.get(keys::save_stem), "tse-foolad", "a set value round-trips");
    check(f.size() == 1, "setting one key makes one entry");

    // Setting the same key again replaces it in place (no duplicate entry).
    f.set(keys::save_stem, "tse-khodro");
    check(f.size() == 1, "re-setting a key keeps one entry");
    check_opt(f.get(keys::save_stem), "tse-khodro", "re-setting a key replaces its value");
}

void test_merge_rules() {
    // Declarations: first writer wins, whatever order they arrive in.
    facts declared;
    facts first;
    first.set(keys::tse_symbol, "foolad");
    declared.merge(first, false);

    facts second;
    second.set(keys::tse_symbol, "khodro");
    declared.merge(second, false);
    check_opt(declared.get(keys::tse_symbol), "foolad", "declarations keep the first writer");

    // Artifacts: last writer wins — the nearest successful segment.
    facts artifacts;
    artifacts.merge(first, true);
    check_opt(artifacts.get(keys::tse_symbol), "foolad", "the first artifact lands");
    artifacts.merge(second, true);
    check_opt(artifacts.get(keys::tse_symbol), "khodro", "a later artifact overwrites");

    // A merge only supplies keys; it never removes what is already there.
    facts extra;
    extra.set(keys::save_stem, "tse-khodro");
    artifacts.merge(extra, true);
    check_opt(artifacts.get(keys::save_stem), "tse-khodro", "merging adds unrelated keys");
    check(artifacts.get(keys::tse_symbol).has_value(), "merging keeps the existing keys");
}

void test_wire_roundtrip() {
    facts f;
    f.set(keys::tse_symbol, "فولاد");
    f.set(keys::save_stem, "tse-فولاد");
    f.set("note", "first line\nsecond line");

    // Encode/decode through a string, exactly like the fork wire.
    auto decoded = facts::decode(f.encode());
    check(decoded.size() == f.size(), "wire: every entry survives");
    check_opt(decoded.get(keys::tse_symbol), "فولاد", "wire: a UTF-8 value survives");
    check_opt(decoded.get("note"), "first line\nsecond line", "wire: a newline inside a value survives");

    // A truncated blob must not read past its end; the codec stops cleanly at
    // the cut (ASan enforces the "no read past the end" half of this).
    auto blob = f.encode();
    std::string const truncated(blob.substr(0, blob.size() - 1));
    (void)facts::decode(truncated);
    check(facts::decode({}).empty(), "wire: decoding an empty blob yields no facts");
    check(!decode_outcome(std::string_view(blob).substr(0, 4)).has_value(), "wire: a truncated outcome is rejected");
    check(!decode_outcome({}).has_value(), "wire: an empty outcome is rejected");
}

void test_outcome_roundtrip() {
    segment_outcome outcome;
    outcome.out = "answer\n";
    outcome.err = "note\n";
    outcome.stderr_buffer = "live\n";
    outcome.published.set(keys::save_stem, "derived");
    outcome.no_clipboard = true;
    outcome.clear = true;
    outcome.redacted = true;
    outcome.failed = false;
    outcome.stdin_consumed = true;
    outcome.stdout_streamed = true;
    outcome.stderr_streamed = true;
    outcome.exit_code = 3;

    auto decoded = decode_outcome(encode_outcome(outcome));
    check(decoded.has_value(), "outcome wire: decodes");
    if (decoded) {
        check_eq(decoded->out, "answer\n", "outcome wire: stdout");
        check_eq(decoded->err, "note\n", "outcome wire: stderr");
        check_eq(decoded->stderr_buffer, "live\n", "outcome wire: stderr buffer");
        check_opt(decoded->published.get(keys::save_stem), "derived", "outcome wire: facts");
        check(decoded->no_clipboard, "outcome wire: no_clipboard");
        check(decoded->clear, "outcome wire: clear");
        check(decoded->redacted, "outcome wire: redacted");
        check(decoded->stdin_consumed, "outcome wire: stdin_consumed");
        check(decoded->stdout_streamed && decoded->stderr_streamed, "outcome wire: streamed flags");
        check(decoded->exit_code == 3, "outcome wire: exit code");
    }
}

// Two children on two pipes: the first writes and exits immediately, the second
// stays silent past the first EOF and then writes more than a pipe buffer. The
// fast child's EOF must not make the loop block on the slow pipe (the old
// implementation tested a stale `revents` and did exactly that), both payloads
// must arrive intact, and every fd must be closed once.
void test_drain_all() {
    std::size_t constexpr slow_payload_size = std::size_t{200} * 1024;
    std::array<int, 2> fast{};
    std::array<int, 2> slow{};
    if (::pipe(fast.data()) != 0 || ::pipe(slow.data()) != 0) {
        check(false, "drain: pipes created");
        return;
    }

    pid_t const fast_child = ::fork();
    if (fast_child == 0) {
        ::close(fast[0]);
        ::close(slow[0]);
        ::close(slow[1]);
        std::string_view const payload = "fast";
        (void)::write(fast[1], payload.data(), payload.size());
        ::close(fast[1]);
        ::_exit(0);
    }

    pid_t const slow_child = ::fork();
    if (slow_child == 0) {
        ::close(fast[0]);
        ::close(fast[1]);
        ::close(slow[0]);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::string const payload(slow_payload_size, 's'); // three times a pipe buffer
        (void)::write(slow[1], payload.data(), payload.size());
        ::close(slow[1]);
        ::_exit(0);
    }

    check(fast_child > 0 && slow_child > 0, "drain: children forked");
    if (fast_child < 0 || slow_child < 0) return;

    ::close(fast[1]);
    ::close(slow[1]);
    std::array<int, 2> const read_fds = {fast[0], slow[0]};
    auto const blobs = drain_all(std::span<int const>(read_fds.data(), read_fds.size()));

    int status = 0;
    ::waitpid(fast_child, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "drain: the fast child exits cleanly");
    ::waitpid(slow_child, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "drain: the slow child exits cleanly");

    check(blobs.size() == 2, "drain: one blob per fd");
    check(blobs.size() == 2 && blobs[0] == "fast", "drain: the fast child keeps its own bytes");
    check(blobs.size() == 2 && blobs[1].size() == slow_payload_size, "drain: the slow child's large blob survives");
    check(blobs.size() == 2 && !blobs[1].empty() && blobs[1].front() == 's' && blobs[1].back() == 's',
          "drain: the slow blob is not mixed with the fast one");

    check(::fcntl(fast[0], F_GETFD) == -1 && errno == EBADF, "drain: the drained fd is closed");
    errno = 0;
    check(::fcntl(slow[0], F_GETFD) == -1 && errno == EBADF, "drain: every fd is closed once");
}

} // namespace

int main() {
    test_basic_get_set();
    test_merge_rules();
    test_wire_roundtrip();
    test_outcome_roundtrip();
    test_drain_all();

    if (failures == 0) {
        std::printf("all facts tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d facts check(s) failed\n", failures);
    return 1;
}
