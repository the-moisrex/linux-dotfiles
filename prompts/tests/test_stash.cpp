// The stash store: name validation, scope paths, the JSON round-trip, append
// merging, shadowing, sanitization, malformed files and the cross-process
// append lock. Everything runs against a scratch PROMPT_STASH_DIR.
#include "../src/sdk/stash.hpp"
#include "../src/sdk/terminal.hpp"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <sys/wait.h>
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

std::filesystem::path scratch() {
    return std::filesystem::temp_directory_path() /
           std::format("prompt-stash-test-{}", static_cast<long long>(::getpid()));
}

// A fresh store per case; the terminal key keeps the `--here` directory stable.
void reset_store() {
    auto const dir = scratch();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ::setenv("PROMPT_STASH_DIR", dir.c_str(), 1);
    ::setenv("PROMPT_TERMINAL_KEY", "test-terminal", 1);
}

stash::record make_record(std::string name, std::string text) {
    stash::record rec;
    rec.name = std::move(name);
    rec.created = stash::now_iso();
    rec.created_ts = stash::now_ts();
    rec.updated = rec.created;
    rec.updated_ts = rec.created_ts;
    rec.text = std::move(text);
    rec.terminal = terminal::current();
    return rec;
}

void test_names() {
    check(stash::valid_name("default"), "a plain name is valid");
    check(stash::valid_name("review-2026"), "a dashed name is valid");
    check(stash::valid_name("فولاد"), "a UTF-8 name is valid");
    check(!stash::valid_name(""), "an empty name is invalid");
    check(!stash::valid_name("a/b"), "a slash is invalid");
    check(!stash::valid_name("a\\b"), "a backslash is invalid");
    check(!stash::valid_name(".."), ".. is invalid");
    check(!stash::valid_name("."), ". is invalid");
    check(!stash::valid_name("-x"), "a leading dash is invalid");
    check(!stash::valid_name("a\nb"), "a control byte is invalid");
}

void test_paths() {
    reset_store();
    auto const root = stash::root();
    check(!root.empty(), "the root is set");
    check(stash::path_for("work", false) == root / "work.json", "the global path");
    auto const scope = terminal::scope(terminal::current());
    check(stash::path_for("work", true) == root / "terminals" / scope / "work.json", "the terminal path");
}

void test_round_trip() {
    reset_store();
    std::string error;
    auto rec = make_record("work", "hello\n");
    rec.known.set("save.stem", "sample");
    rec.redacted = true;
    rec.incomplete = true;
    rec.cwd = "/tmp/project";
    rec.git_root = "/tmp";
    rec.source_run = "run-1";
    check(stash::save(rec, false, false, false, error), "save succeeds");

    auto loaded = stash::load("work", std::nullopt, error);
    check(loaded.has_value(), "load finds the stash");
    if (!loaded) return;
    check_eq(loaded->text, "hello\n", "text round-trips");
    check(loaded->known.get("save.stem") == "sample", "facts round-trip");
    check(loaded->redacted, "the redaction flag round-trips");
    check(loaded->incomplete, "the failure taint round-trips");
    check_eq(loaded->cwd, "/tmp/project", "cwd round-trips");
    check_eq(loaded->git_root, "/tmp", "git root round-trips");
    check_eq(loaded->source_run, "run-1", "source run round-trips");
    check(!loaded->here, "a global load is not terminal-scoped");
}

void test_append() {
    reset_store();
    std::string error;
    auto first = make_record("work", "hello");
    first.known.set("save.stem", "one");
    check(stash::save(first, false, false, false, error), "seed save");
    auto const created = first.created_ts;

    auto second = make_record("work", "world");
    second.known.set("save.stem", "two");
    second.redacted = true;
    check(stash::save(second, false, true, false, error), "append save");

    auto merged = stash::load("work", false, error);
    check(merged.has_value(), "append load");
    if (!merged) return;
    check_eq(merged->text, "hello\n\nworld\n", "append uses the chain's blank-line rule");
    check(merged->known.get("save.stem") == "two", "append facts are last-writer-wins");
    check(merged->redacted, "append ORs the redaction flag");
    check(merged->created_ts == created, "append keeps the original creation time");
}

void test_replace_and_no_clobber() {
    reset_store();
    std::string error;
    check(stash::save(make_record("work", "first"), false, false, false, error), "seed save");
    check(!stash::save(make_record("work", "second"), false, false, true, error), "no-clobber refuses");
    check(!error.empty(), "no-clobber sets an error");
    check(stash::save(make_record("work", "second"), false, false, false, error), "replace save");
    auto loaded = stash::load("work", false, error);
    check(loaded.has_value(), "replace load");
    if (loaded) check_eq(loaded->text, "second", "replace overwrites");
}

void test_scopes() {
    reset_store();
    std::string error;
    check(stash::save(make_record("work", "global"), false, false, false, error), "global save");
    check(stash::save(make_record("work", "here"), true, false, false, error), "terminal save");

    auto effective = stash::load("work", std::nullopt, error);
    check(effective.has_value(), "effective load");
    if (effective) {
        check_eq(effective->text, "here", "the terminal stash shadows the global one");
        check(effective->here, "the effective load is terminal-scoped");
    }
    auto global = stash::load("work", false, error);
    check(global.has_value(), "global load");
    if (global) check_eq(global->text, "global", "the global copy is intact");

    // A name that only exists globally still resolves without --here.
    check(stash::save(make_record("only-global", "g"), false, false, false, error), "global-only save");
    auto only = stash::load("only-global", std::nullopt, error);
    check(only.has_value(), "global fallback");
    if (only) check(!only->here, "the fallback is global");
}

void test_list_and_remove() {
    reset_store();
    std::string error;
    check(stash::save(make_record("alpha", "a"), false, false, false, error), "alpha save");
    check(stash::save(make_record("beta", "b"), true, false, false, error), "beta save");

    std::vector<std::string> errors;
    auto records = stash::list(std::nullopt, errors);
    check(errors.empty(), "list reports no errors");
    check(records.size() == 2, "list returns both scopes");
    bool saw_global = false;
    bool saw_here = false;
    for (auto const& rec : records) {
        if (rec.name == "alpha") saw_global = !rec.here;
        if (rec.name == "beta") saw_here = rec.here;
    }
    check(saw_global, "list includes the global stash");
    check(saw_here, "list includes the terminal stash");

    check(stash::remove("alpha", false, error), "remove the global stash");
    check(!stash::load("alpha", false, error).has_value(), "the removed stash is gone");
    check(!stash::remove("alpha", false, error), "removing twice fails");
}

void test_malformed() {
    reset_store();
    auto const dir = stash::directory(false);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream(dir / "broken.json") << "{not json";
    std::string error;
    auto loaded = stash::load("broken", false, error);
    check(!loaded.has_value(), "a malformed file does not load");
    check(!error.empty(), "a malformed file reports an error");

    std::vector<std::string> errors;
    auto records = stash::list(std::nullopt, errors);
    check(records.empty(), "list skips the malformed file");
    check(errors.size() == 1, "list reports the malformed file");
}

void test_sanitize() {
    reset_store();
    std::string error;
    auto rec = make_record("mail", "contact user@example.com\n");
    rec.known.set("owner", "user@example.com");
    check(stash::save(rec, false, false, false, error), "save a stash with personal data");

    auto loaded = stash::load("mail", false, error);
    check(loaded.has_value(), "load the sanitized stash");
    if (!loaded) return;
    check(loaded->text.contains("EMAIL_ADDRESS"), "the text is redacted");
    check(loaded->redacted, "the text redaction is recorded");
    check(loaded->facts_redacted, "the fact redaction is recorded");
    auto owner = loaded->known.get("owner");
    check(owner.has_value() && owner->contains("EMAIL_ADDRESS"), "the fact is redacted");
}

void test_concurrent_append() {
    reset_store();
    std::string error;
    check(stash::save(make_record("race", "seed"), false, false, false, error), "seed the race stash");

    constexpr int kChildren = 4;
    for (int i = 0; i < kChildren; ++i) {
        pid_t const pid = ::fork();
        if (pid == 0) {
            auto rec = make_record("race", std::format("child-{}", i));
            std::string child_error;
            bool const ok = stash::save(rec, false, true, false, child_error);
            ::_exit(ok ? 0 : 1);
        }
        check(pid > 0, "fork succeeds");
    }
    for (int i = 0; i < kChildren; ++i) {
        int status = 0;
        ::waitpid(-1, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "the child append succeeds");
    }

    auto loaded = stash::load("race", false, error);
    check(loaded.has_value(), "load after concurrent appends");
    if (!loaded) return;
    for (int i = 0; i < kChildren; ++i) {
        check(loaded->text.contains(std::format("child-{}", i)), "every concurrent append is present");
    }
}

} // namespace

int main() {
    try {
        test_names();
        test_paths();
        test_round_trip();
        test_append();
        test_replace_and_no_clobber();
        test_scopes();
        test_list_and_remove();
        test_malformed();
        test_sanitize();
        test_concurrent_append();
    } catch (std::exception const& e) {
        std::fprintf(stderr, "unexpected exception: %s\n", e.what());
        return 1;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d stash test(s) failed\n", failures);
        return 1;
    }
    std::printf("all stash tests passed\n");
    return 0;
}
