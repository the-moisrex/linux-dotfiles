#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_review(prompt_context& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Review this code like a strong practical reviewer.\n";
    output += "Prioritize bugs, behavioral regressions, risky assumptions, edge cases, "
              "maintainability problems, and missing tests.\n";
    output += "List the most important findings first with short explanations, then provide a git "
              "diff for the most useful fixes.\n";
    output += "If there are no major issues, say that clearly and still suggest a small safe "
              "improvement as a git diff if one is worthwhile.\n";
    output += '\n';

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += std::format("File: {}\n\n", file.filename().string());
            output += std::format("```{}\n", infer_lang(file));
            output += trim_context_nl(read_file(file), head_lines);
            output += "```\n\n";
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_review(std::ostream& os) {
    os << R"EOF(Usage: prompt review [--head N] [FILE...]
       some-command | prompt review [--head N] [FILE...]

Review this code like a strong practical reviewer.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("review", "Review this code like a strong practical reviewer.",
                                                &execute_review, &render_help_review);

} // namespace prompt::prompts
