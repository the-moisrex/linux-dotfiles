#include "prompt/prompts/review_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_review(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Review this code like a strong practical reviewer.\n";
    output += "Prioritize bugs, behavioral regressions, risky assumptions, edge cases, "
              "maintainability problems, and missing tests.\n";
    output += "List the most important findings first with short explanations, then provide a git "
              "diff for the most useful fixes.\n";
    output += "If there are no major issues, say that clearly and still suggest a small safe "
              "improvement as a git diff if one is worthwhile.\n";
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
            output += "```\n\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_review(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt review [--head N] [FILE...]
       some-command | prompt review [--head N] [FILE...]

Review this code like a strong practical reviewer.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
