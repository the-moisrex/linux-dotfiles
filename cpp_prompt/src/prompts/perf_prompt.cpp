#include "prompt/prompts/perf_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_perf(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Review this for performance issues.\n";
    output += "Look for unnecessary allocations, wasteful copies, bad algorithms, blocking work, "
              "repeated parsing, excessive syscalls, poor data layout, and avoidable hot-path "
              "overhead.\n";
    output += "Call out which issues matter most in practice, and provide a git diff for the "
              "smallest high-impact improvements.\n";
    output += "Do not sacrifice correctness or readability for tiny wins.\n";
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            output += "```\n";
            output += "\n";
        } else {
            error += "Warning: File not found or is not a regular file: " + std::string(ctx.args[i]) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_perf(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt perf [--head N] [FILE...]
       some-command | prompt perf [--head N] [FILE...]

Review this for performance issues.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
