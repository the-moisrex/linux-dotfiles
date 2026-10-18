#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_perf(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output = R"PROMPT(Review this for performance issues.
Look for unnecessary allocations, wasteful copies, bad algorithms, blocking work, repeated parsing, excessive syscalls, poor data layout, and avoidable hot-path overhead.
Call out which issues matter most in practice, and provide a git diff for the smallest high-impact improvements.
Do not sacrifice correctness or readability for tiny wins.

)PROMPT";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += embed_file_block(file, opts);
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File not found or is not a regular file: {}\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_perf(std::ostream& os) {
    os << file_usage_help("perf") << R"EOF(
Review this for performance issues.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("perf", "Review this for performance issues.", &execute_perf, &render_help_perf);

} // namespace prompt::prompts
