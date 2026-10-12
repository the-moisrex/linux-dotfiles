#include "prompt/prompts/prompts.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_output execute_tests(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Review this code and identify the highest-value missing tests.\n";
    output += "Focus on edge cases, regressions, error handling, boundary conditions, invalid "
              "input, and behavior that looks easy to break.\n";
    output += "Propose a minimal but effective test plan first, then provide a git diff that adds "
              "or updates the tests.\n";
    output += "Prefer the smallest practical diff that materially improves confidence.\n";
    output += "\n";

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
            output += "\n```\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) +
                     "' not found or is not a "
                     "regular file.\n";
        }
    }

    return {std::move(output), std::move(error), false, false};
}

void render_help_tests(std::ostream& os) {
    os << R"EOF(Usage: prompt tests [--head N] [FILE...]
       some-command | prompt tests [--head N] [FILE...]

Review this code and identify the highest-value missing tests.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
