#include "prompt/prompts/hoist_if_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_hoist_if(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Analyze the code below and find opportunities to move if statements outside of "
              "for/while loops.\n";
    output += "Focus on conditions that are loop-invariant: the condition's result does not "
              "change across iterations.\n";
    output += "For each opportunity:\n";
    output += "  1. Explain why the condition is invariant (what makes it safe to hoist).\n";
    output += "  2. Provide a git diff showing the refactored code.\n";
    output += "  3. Note any caveats (readability trade-offs, cases where hoisting is invalid).\n";
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
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            output += "\n```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found or is not a regular file.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_hoist_if(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt hoist-if [--head N] [FILE...]
       some-command | prompt hoist-if [--head N] [FILE...]

Hoist invariant if statements out of loops for performance.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
