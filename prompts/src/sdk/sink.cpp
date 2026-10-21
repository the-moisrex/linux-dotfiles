#include "sink.hpp"
#include <charconv>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace prompt {

namespace {

bool parse_failure(std::string_view value, sink_failure& out) {
    if (value == "abort") {
        out = sink_failure::abort;
    } else if (value == "continue") {
        out = sink_failure::proceed;
    } else if (value == "skip") {
        out = sink_failure::skip;
    } else {
        return false;
    }
    return true;
}

bool parse_head(std::string_view value, std::optional<std::size_t>& out) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string_view::npos) return false;
    std::size_t parsed = 0;
    auto const* begin = value.data();
    auto const* end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) return false;
    out = parsed;
    return true;
}

} // namespace

sink_args parse_sink_args(std::span<std::string_view const> args) {
    sink_args parsed;
    bool positional = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view arg = args[i];
        if (positional) {
            parsed.rest.push_back(arg);
            continue;
        }
        if (arg == "--") {
            positional = true;
            parsed.rest.push_back(arg);
            continue;
        }
        if (arg == "--with-context") {
            parsed.options.with_context = true;
            continue;
        }
        if (arg == "--show-context") {
            parsed.options.show_context = true;
            continue;
        }
        if (arg == "--on-error" || arg.starts_with("--on-error=")) {
            std::string_view value;
            if (arg == "--on-error") {
                if (i + 1 >= args.size()) {
                    parsed.error = "Missing value for --on-error\n";
                    return parsed;
                }
                value = args[++i];
            } else {
                value = arg.substr(std::string_view("--on-error=").size());
            }
            if (!parse_failure(value, parsed.options.failure)) {
                parsed.error = "--on-error requires abort, continue, or skip\n";
                return parsed;
            }
            continue;
        }
        if (arg == "--head" || arg.starts_with("--head=")) {
            std::string_view value;
            if (arg == "--head") {
                if (i + 1 >= args.size()) {
                    parsed.error = "Missing value for --head\n";
                    return parsed;
                }
                value = args[++i];
            } else {
                value = arg.substr(std::string_view("--head=").size());
            }
            if (!parse_head(value, parsed.options.head_lines)) {
                parsed.error = "--head requires a non-negative integer\n";
                return parsed;
            }
            continue;
        }
        parsed.rest.push_back(arg);
    }
    return parsed;
}

void render_sink_help(std::ostream& out) {
    out << "Options:\n"
           "  --on-error MODE   abort (default), continue, or skip if the batch feeding this sink failed\n"
           "  --with-context    keep the pre-sink context for later prompts as well as this answer\n"
           "  --show-context    also print the source prompts in the final output\n"
           "  --head N          keep only the first N lines of the context\n";
}

std::string_view wait_for_prompts(prompt_context const& ctx) { return ctx.chain_context; }

std::string_view trim_context_lines(std::string_view text, std::size_t limit) {
    std::size_t start = 0;
    std::size_t lines = 0;
    while (start < text.size() && lines < limit) {
        auto const newline = text.find('\n', start);
        if (newline == std::string_view::npos) return text;
        start = newline + 1;
        ++lines;
    }
    return text.substr(0, start);
}

void write_stdout(prompt_context& ctx, std::string_view chunk) {
    ctx.out.append(chunk);
    if (ctx.stream_stdout && ctx.stdout_writer != nullptr) {
        ctx.stdout_writer(chunk);
        ctx.stdout_streamed = true;
    }
}

void write_stderr(prompt_context& ctx, std::string_view chunk) {
    ctx.stderr_buffer.append(chunk);
    if (ctx.stream_stderr && ctx.stderr_writer != nullptr) {
        ctx.stderr_writer(chunk);
        ctx.stderr_streamed = true;
    }
}

process::command_result run_sink_tool(prompt_context& ctx, std::vector<std::string> argv, std::string stdin_data,
                                      std::chrono::milliseconds timeout) {
    process::command cmd{std::move(argv)};
    cmd.stdin_data(std::move(stdin_data));
    cmd.timeout(timeout);
    cmd.on_stdout([&ctx](std::string_view chunk) { write_stdout(ctx, chunk); });
    cmd.on_stderr([&ctx](std::string_view chunk) { write_stderr(ctx, chunk); });
    return cmd.run();
}

} // namespace prompt
