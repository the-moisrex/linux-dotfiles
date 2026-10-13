#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_output execute_hoist_if(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Analyze the code below and find opportunities to move if statements outside of "
              "for/while loops.\n";
    output += "Focus on conditions that are loop-invariant: the condition's result does not "
              "change across iterations.\n";
    output += "For each opportunity:\n  1. Explain why the condition is invariant (what makes it safe to hoist).\n  2. "
              "Provide a git diff showing the refactored code.\n  3. Note any caveats (readability trade-offs, cases "
              "where hoisting is invalid).\n";
    output += "Do not hoist conditions that depend on the loop variable or mutate state inside "
              "the loop.\n";
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
            output += "\n```\n\n";
        } else {
            error += std::format("Warning: File '{}' not found or is not a regular file.\n", std::string(ctx.args[i]));
        }
    }

    return {std::move(output), std::move(error), false, false};
}

void render_help_hoist_if(std::ostream& os) {
    os << R"EOF(Usage: prompt hoist-if [--head N] [FILE...]
       some-command | prompt hoist-if [--head N] [FILE...]

Hoist invariant if statements out of loops for performance.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
