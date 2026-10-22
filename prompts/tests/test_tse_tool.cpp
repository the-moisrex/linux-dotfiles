#include "../src/prompts/tse_data.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace prompt;
namespace tse_data = prompt::prompts::tse_data;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

std::string failure_of(std::vector<std::string> const& argv, std::chrono::milliseconds timeout) {
    try {
        tse_data::run_tool("tse", "bin/tse", argv, timeout);
    } catch (soft_error const& e) {
        return e.what();
    }
    return {};
}

void test_timeout_is_reported() {
    // Regression: a killed fetch used to throw an empty message, so `.tse`
    // exited 1 without printing anything at all.
    auto const message = failure_of({"sleep", "5"}, std::chrono::milliseconds(100));
    check(message.contains("prompt tse: bin/tse timed out after"), "timeout message names the tool");
    check(message.contains("retry with a narrower request"), "timeout message suggests a remedy");
}

void test_nonzero_exit_is_reported() {
    auto const message = failure_of({"false"}, std::chrono::seconds(10));
    check(message == "prompt tse: bin/tse exited with status 1\n", "non-zero exit message");
}

void test_empty_output_is_reported() {
    // An empty dataset must fail here, not reach `.save` or an AI sink as an
    // empty context.
    auto const message = failure_of({"true"}, std::chrono::seconds(10));
    check(message == "prompt tse: bin/tse returned no data\n", "empty output message");
}

void test_success_is_returned() {
    try {
        auto const result = tse_data::run_tool("tse", "bin/tse", {"printf", "data"}, std::chrono::seconds(10));
        check(result.stdout_data == "data", "successful run returns stdout");
    } catch (prompt_error const& e) {
        std::fprintf(stderr, "unexpected failure: %s", e.what());
        ++failures;
    }
}

} // namespace

int main() {
    test_timeout_is_reported();
    test_nonzero_exit_is_reported();
    test_empty_output_is_reported();
    test_success_is_returned();
    if (failures != 0) {
        std::fprintf(stderr, "test_tse_tool: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
