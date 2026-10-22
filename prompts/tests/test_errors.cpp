#include "../src/sdk/errors.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace prompt;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The exception surface the dispatcher (main.cpp) relies on: every failing
// prompt throws a prompt_error that carries the exact stdout/stderr/exit code
// the dispatcher reports, and the chain keeps running.
void test_exception_surface() {
    // usage_error maps to exit 2.
    usage_error usage{"Usage: prompt x\n"};
    check(usage.exit_code() == 2, "usage_error exits 2");
    check(std::string(usage.what()) == "Usage: prompt x\n", "usage_error keeps its message");
    check(usage.partial_output().empty(), "usage_error defaults to no stdout");

    // soft_error maps to exit 1.
    soft_error soft{"tse: boom\n"};
    check(soft.exit_code() == 1, "soft_error exits 1");
    check(soft.partial_output().empty(), "soft_error defaults to no stdout");

    // prompt_error carries arbitrary statuses plus the flags the chain needs.
    prompt_error err{130, "partial failure", "already printed\n", /*no_clipboard=*/true, /*stdin_consumed=*/true};
    check(err.exit_code() == 130, "prompt_error keeps its exit code");
    check(err.partial_output() == "already printed\n", "prompt_error keeps its partial stdout");
    check(err.no_clipboard(), "prompt_error keeps the clipboard opt-out");
    check(err.stdin_consumed(), "prompt_error keeps the stdin-spent flag");
    check(std::string(err.what()) == "partial failure", "prompt_error keeps its stderr message");

    // Both derived classes are catchable as prompt_error (main.cpp has a
    // single catch site).
    bool caught = false;
    try {
        throw soft_error{"boom\n"};
    } catch (prompt_error const& e) {
        caught = e.exit_code() == 1;
        check(std::string(e.what()) == "boom\n", "caught soft_error through the base");
    }
    check(caught, "soft_error is catchable as prompt_error");

    // A prompt that printed context before failing keeps that stdout.
    usage_error with_output{"Unknown option: --nope\n", "prompt text so far\n"};
    check(with_output.partial_output() == "prompt text so far\n", "partial output travels with usage_error");
    check(with_output.exit_code() == 2, "usage_error with partial output still exits 2");
}

} // namespace

int main() {
    test_exception_surface();
    if (failures == 0) {
        std::printf("all error-surface tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d error-surface checks failed\n", failures);
    return 1;
}
