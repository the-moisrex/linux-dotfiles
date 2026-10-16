#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_optimize_prompt(prompt_context& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Act as an expert prompt engineer. Review the provided prompt(s) or instructions "
              "below.\n";
    output += "Your goal is to optimize the input to create a clearer, more effective, and more "
              "robust prompt for an AI assistant.\n";
    output += "\nProvide:\n";
    output += "1. A brief critique of the original prompt (identifying ambiguities, missing "
              "context, or structural issues).\n";
    output += "2. A checklist of the specific improvements made.\n";
    output += "3. The completely rewritten, optimized prompt enclosed in a markdown code block so "
              "it can be easily copied.\n";
    output += "\nHere is the input to optimize:\n\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            // optimize-prompt.sh puts no blank line between "File:" and the fence.
            output += std::format("File: {}\n", file.filename().string());
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

static void render_help_optimize_prompt(std::ostream& os) {
    os << R"EOF(Usage: prompt optimize-prompt [--head N] [FILE...]
       echo "my rough prompt" | prompt optimize-prompt [--head N] [FILE...]

Ask the AI to analyze and improve an input prompt.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("optimize-prompt", "Ask the AI to analyze and improve an input prompt.",
                                                &execute_optimize_prompt, &render_help_optimize_prompt);

} // namespace prompt::prompts
