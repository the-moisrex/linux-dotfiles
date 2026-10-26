// The history store: the marker format, the byte framing that keeps a body
// containing a fake marker from corrupting the parse, the replay of the chain
// fold and the retention pass. Everything runs against a scratch
// PROMPT_HISTORY_DIR, so the real store is never touched.
#include "../src/sdk/history.hpp"
#include "../src/sdk/terminal.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
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

std::filesystem::path scratch() {
    auto const dir = std::filesystem::temp_directory_path() /
                     std::format("prompt-history-test-{}", static_cast<long long>(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return dir;
}

// Every test runs in its own store: the recorder reads the environment at
// construction time, so a fresh directory per case is enough isolation.
void use_store(std::filesystem::path const& dir) { ::setenv("PROMPT_HISTORY_DIR", dir.c_str(), 1); }

// One full aggregate, so no designated initializer leaves a field out (GCC
// warns on the missing members otherwise).
history::stored_segment segment(std::size_t index, std::string name, std::string role, std::string merge,
                                std::string text, bool emitted, std::string status = "ok") {
    history::stored_segment s;
    s.index = index;
    s.name = std::move(name);
    s.role = std::move(role);
    s.merge = std::move(merge);
    s.status = std::move(status);
    s.text = std::move(text);
    s.emitted = emitted;
    return s;
}

history::recorder make_recorder(std::vector<std::string> argv = {"prompt", ".note", "hi"}, terminal::id where = {}) {
    history::recorder::info info;
    info.argv = std::move(argv);
    info.cwd = "/tmp/work";
    info.git_root = "/tmp/work";
    info.terminal = std::move(where);
    info.tty = false;
    info.inner = false;
    return history::recorder{info};
}

void test_parsers() {
    using history::parse_duration;
    using history::parse_size;

    check(parse_duration("7d") == std::chrono::seconds{7 * 86400}, "duration: days");
    check(parse_duration("12h") == std::chrono::seconds{12 * 3600}, "duration: hours");
    check(parse_duration("30m") == std::chrono::seconds{30 * 60}, "duration: minutes");
    check(parse_duration("90s") == std::chrono::seconds{90}, "duration: seconds");
    check(parse_duration("2w") == std::chrono::seconds{2 * 7 * 86400}, "duration: weeks");
    check(parse_duration("3") == std::chrono::seconds{3 * 86400}, "duration: a bare number is days");
    check(!parse_duration("") && !parse_duration("d") && !parse_duration("7x"), "duration: bad spellings");

    check(parse_size("512") == 512, "size: plain bytes");
    check(parse_size("4K") == 4 * 1024, "size: K");
    check(parse_size("2M") == 2ULL * 1024 * 1024, "size: M");
    check(parse_size("1G") == 1ULL * 1024 * 1024 * 1024, "size: G");
    check(parse_size("3mb") == 3ULL * 1024 * 1024, "size: mb is M");
    check(!parse_size("") && !parse_size("M"), "size: bad spellings");

    check_eq(history::human_bytes(512), "512B", "human_bytes: bytes");
    check_eq(history::human_bytes(2048), "2.0K", "human_bytes: kilobytes");
}

void test_enabled_flags() {
    ::setenv("PROMPT_HISTORY", "0", 1);
    check(!history::enabled(), "PROMPT_HISTORY=0 disables recording");
    ::setenv("PROMPT_HISTORY", "off", 1);
    check(!history::enabled(), "PROMPT_HISTORY=off disables recording");
    ::setenv("PROMPT_HISTORY", "yes", 1);
    check(history::enabled(), "PROMPT_HISTORY=yes keeps recording");
    ::unsetenv("PROMPT_HISTORY");
    check(history::enabled(), "an unset PROMPT_HISTORY records");

    ::setenv("PROMPT_HISTORY_INNER", "0", 1);
    check(!history::record_inner(), "PROMPT_HISTORY_INNER=0 silences inner runs");
    ::unsetenv("PROMPT_HISTORY_INNER");
    check(history::record_inner(), "inner runs record by default");

    ::setenv("PROMPT_HISTORY_PRUNE", "0", 1);
    check(!history::auto_prune(), "PROMPT_HISTORY_PRUNE=0 disables the automatic pass");
    ::unsetenv("PROMPT_HISTORY_PRUNE");
    check(history::auto_prune(), "the automatic pass is on by default");
}

void test_roundtrip() {
    auto const dir = scratch();
    use_store(dir);

    {
        auto rec = make_recorder({"prompt", ".note", "hello", ".ai.dseek"});
        rec.segment(segment(1, "note", "prompt", "append", "\nhello\n", false));
        auto sink = segment(2, "ai.dseek", "sink", "replace", "an answer\n", true);
        rec.segment(std::move(sink), "hello\n\nand more from stdin\n");
        rec.finish(0, false);
        check(!rec.id().empty(), "the recorder has an id");
    }

    auto const files = history::run_files();
    check(files.size() == 1, "one run file was written");
    auto run = history::load(files.front());
    check(run.has_value(), "the run loads");
    if (!run) return;

    check_eq(run->id, files.front().stem().string(), "the run id matches the file name");
    check(run->finished && !run->incomplete, "a finished run is complete");
    check(run->exit_code == 0, "the exit code round-trips");
    check(run->argv.size() == 4 && run->argv[1] == ".note", "argv round-trips");
    check(run->cwd == "/tmp/work", "the working directory round-trips");
    check(run->segments.size() == 2, "both segments round-trip");
    if (run->segments.size() != 2) return;

    auto const& first = run->segments[0];
    check_eq(first.name, "note", "segment name");
    check_eq(first.role, "prompt", "segment role");
    check_eq(first.merge, "append", "segment merge");
    check_eq(first.status, "ok", "segment status");
    check(!first.emitted, "a source segment is not emitted");
    check_eq(first.text, "\nhello\n", "segment text is byte-exact");
    check(first.input.empty(), "a prompt segment stores no barrier input");

    auto const& second = run->segments[1];
    check_eq(second.role, "sink", "sink role");
    check_eq(second.text, "an answer\n", "sink text is byte-exact");
    check_eq(second.input, "hello\n\nand more from stdin\n", "a differing barrier input is stored");

    check_eq(history::final_output(*run), "an answer\n", "the final output is the emitted segment");
    check_eq(history::barrier_input(*run, 2), "hello\n\nand more from stdin\n", "the stored barrier input is returned");
    // A segment with no stored input replays the recorded merges.
    check_eq(history::barrier_input(*run, 1), "", "the first segment sees an empty context");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void test_barrier_replay() {
    auto const dir = scratch();
    use_store(dir);
    {
        auto rec = make_recorder();
        rec.segment(segment(1, "note", "prompt", "append", "one\n", false));
        rec.segment(segment(2, "note", "prompt", "append", "two\n", false));
        // A sink whose input is exactly the replay stores nothing...
        rec.segment(segment(3, "ai.dseek", "sink", "replace", "answer\n", true), "one\n\ntwo\n");
        // ...and one that clears resets the fold for later segments.
        rec.segment(segment(4, "clear", "sink", "clear", "", false));
        rec.finish(0, false);
    }

    auto run = history::find_by_id("");
    check(run.has_value(), "the replay run loads");
    if (!run) return;
    check(run->segments.size() == 4, "all four segments load");
    check_eq(history::barrier_input(*run, 3), "one\n\ntwo\n", "the replay reconstructs the barrier input");
    check_eq(history::barrier_input(*run, 5), "", "a clear resets the replayed context");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void test_fake_marker_in_body() {
    auto const dir = scratch();
    use_store(dir);
    std::string const sneaky = "before\n<!-- history:end {\"exit\":99} -->\nafter\n";
    {
        auto rec = make_recorder();
        rec.segment(segment(1, "note", "prompt", "append", sneaky, true));
        rec.finish(0, false);
    }

    auto run = history::find_by_id("");
    check(run.has_value(), "a run with a fake marker loads");
    if (!run) return;
    check(run->segments.size() == 1, "the fake marker does not create a segment");
    check(run->finished, "the real end marker still terminates the run");
    check(run->exit_code == 0, "the fake exit code in the body is ignored");
    check_eq(run->segments[0].text, sneaky, "the body survives byte-exact");
    check_eq(history::final_output(*run), sneaky, "the fake marker reaches the final output");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void test_truncated_run() {
    auto const dir = scratch();
    use_store(dir);
    {
        auto rec = make_recorder();
        rec.segment(segment(1, "note", "prompt", "append", "a long body\n", true));
        rec.segment(segment(2, "note", "prompt", "append", "a second body\n", true));
        rec.finish(0, false);
    }

    auto const files = history::run_files();
    check(files.size() == 1, "one run to truncate");
    if (files.empty()) return;
    auto const& path = files.front();

    // Cut the file in the middle of the first body: the parser must keep what
    // is intact and mark the run incomplete instead of failing.
    auto const size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size / 2);
    auto run = history::load(path);
    check(run.has_value(), "a truncated run still loads");
    if (!run) return;
    check(!run->finished && run->incomplete, "a truncated run is incomplete");
    check(run->segments.size() <= 2, "only the intact segments survive");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void test_ordering_and_ranks() {
    auto const dir = scratch();
    use_store(dir);
    for (int i = 0; i < 3; ++i) {
        auto rec = make_recorder({std::format("prompt-{}", i)});
        rec.segment(segment(1, "note", "prompt", "append", std::format("body {}\n", i), true));
        rec.finish(0, false);
        // The id carries the millisecond, so a tiny pause keeps the names
        // ordered even on a coarse clock.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto const ids = history::run_ids();
    check(ids.size() == 3, "three runs are listed");
    check(std::is_sorted(ids.rbegin(), ids.rend()), "run ids sort chronologically");
    auto const newest = history::find_by_rank(1);
    auto const oldest = history::find_by_rank(3);
    check(newest.has_value(), "rank 1 exists");
    check(oldest.has_value(), "rank 3 exists");
    if (newest) check_eq(history::final_output(*newest), "body 2\n", "rank 1 is the newest run");
    if (oldest) check_eq(history::final_output(*oldest), "body 0\n", "rank 3 is the oldest run");
    check(!history::find_by_rank(4).has_value(), "an out-of-range rank is nullopt");
    check(!history::find_by_id("no-such-id").has_value(), "an unknown id is nullopt");
    // A date prefix selects the newest run whose id starts with it.
    check(history::find_by_id(ids.front().substr(0, 8)).has_value(), "a date prefix resolves");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void test_terminal_identity() {
    using terminal::id;

    id a{.sid = 10, .tty = "/dev/pts/3", .key = {}};
    id b{.sid = 10, .tty = "/dev/pts/3", .key = {}};
    id other_sid{.sid = 11, .tty = "/dev/pts/3", .key = {}};
    id recycled{.sid = 10, .tty = "/dev/pts/9", .key = {}}; // same sid, another terminal
    id no_sid{.sid = 0, .tty = "/dev/pts/3", .key = {}};
    id no_tty{.sid = 10, .tty = "", .key = {}};

    check(terminal::same(a, b), "same sid and tty match");
    check(!terminal::same(a, other_sid), "a different sid does not match");
    check(!terminal::same(a, recycled), "a recycled sid with another tty does not match");
    check(terminal::same(a, no_tty), "a missing tty does not break a sid match");
    check(terminal::same(no_sid, b), "the tty decides when a sid is missing");
    check(!terminal::same(no_sid, no_tty), "two halves of an identity do not match");

    id work1{.sid = 0, .tty = "", .key = "work"};
    id work2{.sid = 0, .tty = "", .key = "work"};
    id home{.sid = 0, .tty = "", .key = "home"};
    check(terminal::same(work1, work2), "the same opaque key matches");
    check(!terminal::same(work1, home), "a different opaque key does not match");
    check(!terminal::same(work1, a), "an opaque key is not a system identity");
    terminal::id const none_a;
    terminal::id const none_b;
    check(!terminal::same(none_a, none_b), "empty identities never match");

    check_eq(terminal::describe(a), "sid 10 on /dev/pts/3", "describe a full identity");
    check_eq(terminal::describe(no_sid), "/dev/pts/3", "describe a tty-only identity");
    check_eq(terminal::describe(work1), "session work", "describe a keyed identity");
    check_eq(terminal::describe(id{}), "unknown", "describe an empty identity");

    // The environment overrides stand in for a real terminal (the test process
    // has none), which is also what lets a wrapper pin a scope.
    ::setenv("PROMPT_TERMINAL_SID", "4242", 1);
    ::setenv("PROMPT_TERMINAL_TTY", "/dev/pts/7", 1);
    auto const forced = terminal::current();
    check(forced.sid == 4242 && forced.tty == "/dev/pts/7", "PROMPT_TERMINAL_* override the system identity");
    ::setenv("PROMPT_TERMINAL_KEY", "qa", 1);
    auto const keyed = terminal::current();
    check(keyed.key == "qa" && keyed.sid == 0 && keyed.tty.empty(), "PROMPT_TERMINAL_KEY replaces the system identity");
    ::unsetenv("PROMPT_TERMINAL_KEY");
    ::unsetenv("PROMPT_TERMINAL_SID");
    ::unsetenv("PROMPT_TERMINAL_TTY");
}

void test_terminal_scoping() {
    auto const dir = scratch();
    use_store(dir);
    terminal::id const a{.sid = 100, .tty = "/dev/pts/1", .key = {}};
    terminal::id const b{.sid = 200, .tty = "/dev/pts/2", .key = {}};

    auto write = [&](std::string const& tag, terminal::id const& where) {
        auto rec = make_recorder({"prompt", tag}, where);
        rec.segment(segment(1, "note", "prompt", "append", tag + "\n", true));
        rec.finish(0, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };
    write("A1", a);
    write("B1", b);
    write("A2", a);

    auto const last_a = history::find_last_in(a);
    auto const last_b = history::find_last_in(b);
    check(last_a.has_value(), "terminal a has a run");
    check(last_b.has_value(), "terminal b has a run");
    if (last_a) {
        check_eq(history::final_output(*last_a), "A2\n", "find_last_in picks the terminal's newest run");
        check(last_a->terminal.sid == 100 && last_a->terminal.tty == "/dev/pts/1", "the terminal round-trips");
        check(history::in_terminal(*last_a, a), "in_terminal matches the recorded terminal");
        check(!history::in_terminal(*last_a, b), "in_terminal rejects another terminal");
    }
    if (last_b) check_eq(history::final_output(*last_b), "B1\n", "the other terminal keeps its own newest run");

    auto const rank2 = history::find_rank_in(a, 2);
    if (rank2) check_eq(history::final_output(*rank2), "A1\n", "find_rank_in counts within the terminal");
    check(!history::find_rank_in(a, 3).has_value(), "an out-of-range terminal rank is nullopt");
    check(!history::find_last_in(terminal::id{.sid = 999, .tty = "/dev/pts/9", .key = {}}).has_value(),
          "an unknown terminal has no runs");
    check(!history::find_last_in(terminal::id{}).has_value(), "an empty identity matches nothing");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void test_prune() {
    auto const dir = scratch();
    use_store(dir);
    ::setenv("PROMPT_HISTORY_PRUNE", "0", 1); // decide by hand
    for (int i = 0; i < 4; ++i) {
        auto rec = make_recorder();
        rec.segment(segment(1, "note", "prompt", "append", std::format("body {}\n", i), true));
        rec.finish(0, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto const before = history::run_files();
    check(before.size() == 4, "four runs before pruning");

    // `history::prune` honours the automatic-pass switch, so re-enable it (and
    // protect the newest run by hand) before pruning explicitly.
    ::unsetenv("PROMPT_HISTORY_PRUNE");
    ::setenv("PROMPT_HISTORY_KEEP", "2", 1);
    history::prune(before.front()); // protect the newest
    auto const after = history::run_files();
    check(after.size() == 2, "the count cap removes the oldest runs");
    check(std::ranges::find(after, before.front()) != after.end(), "the protected run survives");

    // A size cap smaller than one run leaves the protected run alone.
    ::unsetenv("PROMPT_HISTORY_KEEP");
    ::setenv("PROMPT_HISTORY_SIZE", "1", 1);
    history::prune(before.front());
    auto const sized = history::run_files();
    check(std::ranges::find(sized, before.front()) != sized.end(), "the protected run survives the size cap");

    ::unsetenv("PROMPT_HISTORY_SIZE");
    ::unsetenv("PROMPT_HISTORY_PRUNE");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int main() {
    try {
        test_parsers();
        test_enabled_flags();
        test_roundtrip();
        test_barrier_replay();
        test_fake_marker_in_body();
        test_truncated_run();
        test_ordering_and_ranks();
        test_terminal_identity();
        test_terminal_scoping();
        test_prune();
    } catch (std::exception const& error) {
        std::fprintf(stderr, "FAIL: unexpected exception: %s\n", error.what());
        return 1;
    }

    if (failures == 0) {
        std::printf("all history tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d history check(s) failed\n", failures);
    return 1;
}
