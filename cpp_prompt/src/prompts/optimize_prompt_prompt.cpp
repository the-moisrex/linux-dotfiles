#include "prompt/prompts/optimize_prompt_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_optimize_prompt(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Act as an expert prompt engineer. Review the provided prompt(s) or instructions "
              "below.\n";
    output += "Your goal is to optimize the input to create a clearer, more effective, and more "
              "robust prompt for an AI assistant.\n";
    output += "\n";
    output += "Provide:\n";
    output += "1. A brief critique of the original prompt (identifying ambiguities, missing "
              "context, or structural issues).\n";
    output += "2. A checklist of the specific improvements made.\n";
    output += "3. The completely rewritten, optimized prompt enclosed in a markdown code block so "
              "it can be easily copied.\n";
    output += "\n";
    output += "Here is the input to optimize:\n";
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            // optimize-prompt.sh puts no blank line between "File:" and the fence.
            output += "File: " + file.filename().string() + "\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            output += "```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_optimize_prompt(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt optimize-prompt [--head N] [FILE...]
       echo "my rough prompt" | prompt optimize-prompt [--head N] [FILE...]

Ask the AI to analyze and improve an input prompt.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
