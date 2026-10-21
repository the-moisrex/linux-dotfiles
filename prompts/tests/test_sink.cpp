#include "../src/sdk/sink.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

using namespace prompt;

namespace {

int failures = 0;

std::string streamed_stdout;
std::string streamed_stderr;

void collect_stdout(std::string_view chunk) { streamed_stdout.append(chunk); }
void collect_stderr(std::string_view chunk) { streamed_stderr.append(chunk); }

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_sink_flags() {
    std::array<std::string_view, 9> args = {
        "--with-context", "--on-error", "continue", "--head=12", "--model", "fast", "hello", "--show-context", "--",
    };
    auto parsed = parse_sink_args(std::span<std::string_view const>(args.data(), args.size()));
    check(parsed.error.empty(), "sink flags: valid flags parse");
    check(parsed.options.with_context, "sink flags: --with-context");
    check(parsed.options.show_context, "sink flags: --show-context");
    check(parsed.options.failure == sink_failure::proceed, "sink flags: --on-error continue");
    check(parsed.options.head_lines && *parsed.options.head_lines == 12, "sink flags: --head=N");
    check(parsed.rest.size() == 4, "sink flags: provider args stay in rest");
    check(parsed.rest[0] == "--model", "sink flags: rest keeps --model");
    check(parsed.rest[1] == "fast", "sink flags: rest keeps the model value");
    check(parsed.rest[2] == "hello", "sink flags: rest keeps positional text");
    check(parsed.rest[3] == "--", "sink flags: rest keeps the separator");
}

void test_sink_double_dash() {
    std::array<std::string_view, 3> args = {"--", "--with-context", "x"};
    auto parsed = parse_sink_args(std::span<std::string_view const>(args.data(), args.size()));
    check(parsed.error.empty(), "sink --: no error");
    check(!parsed.options.with_context, "sink --: stops sink-flag parsing");
    check(parsed.rest.size() == 3, "sink --: everything after -- is kept");
}

void test_sink_errors() {
    std::array<std::string_view, 2> bad_mode = {"--on-error", "nope"};
    auto parsed = parse_sink_args(std::span<std::string_view const>(bad_mode.data(), bad_mode.size()));
    check(!parsed.error.empty(), "sink flags: invalid --on-error rejected");

    std::array<std::string_view, 2> bad_head = {"--head", "x"};
    parsed = parse_sink_args(std::span<std::string_view const>(bad_head.data(), bad_head.size()));
    check(!parsed.error.empty(), "sink flags: invalid --head rejected");

    std::array<std::string_view, 1> missing = {"--on-error"};
    parsed = parse_sink_args(std::span<std::string_view const>(missing.data(), missing.size()));
    check(!parsed.error.empty(), "sink flags: missing --on-error value rejected");
}

void test_sink_head_trim() {
    check(trim_context_lines("a\nb\nc\n", 0).empty(), "sink head: zero yields empty");
    check(trim_context_lines("a\nb\nc\n", 1) == "a\n", "sink head: keeps the first line");
    check(trim_context_lines("a\nb\nc\n", 2) == "a\nb\n", "sink head: keeps two lines");
    check(trim_context_lines("a\nb\nc\n", 9) == "a\nb\nc\n", "sink head: more than available keeps all");
    check(trim_context_lines("a\nb", 2) == "a\nb", "sink head: final line without newline");
}

void test_sink_tool_streaming() {
    prompt_context ctx;
    ctx.stream_stdout = true;
    ctx.stream_stderr = true;
    ctx.stdout_writer = &collect_stdout;
    ctx.stderr_writer = &collect_stderr;
    streamed_stdout.clear();
    streamed_stderr.clear();

    auto result = run_sink_tool(ctx, {"sh", "-c", "printf out; printf err >&2"}, "");
    check(result.exit_code == 0, "sink tool: exit code");
    check(ctx.out == "out", "sink tool: stdout retained in ctx.out");
    check(ctx.stderr_buffer == "err", "sink tool: stderr retained separately");
    check(streamed_stdout == "out", "sink tool: stdout streamed");
    check(streamed_stderr == "err", "sink tool: stderr streamed");
    check(ctx.stdout_streamed, "sink tool: stdout streamed flag");
    check(ctx.stderr_streamed, "sink tool: stderr streamed flag");

    auto timed = run_sink_tool(ctx, {"sh", "-c", "exec sleep 5"}, "", std::chrono::milliseconds{50});
    check(timed.timed_out, "sink tool: timeout kills a silent child");
    check(timed.exit_code == -1, "sink tool: timed-out child has no exit code");
}

} // namespace

int main() {
    test_sink_flags();
    test_sink_double_dash();
    test_sink_errors();
    test_sink_head_trim();
    test_sink_tool_streaming();

    if (failures == 0) {
        std::printf("all sink tests passed\n");
        return 0;
    }
    return 1;
}
