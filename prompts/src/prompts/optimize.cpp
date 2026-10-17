#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_optimize_prompt(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

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
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        // optimize-prompt.sh puts no blank line between "File:" and the fence.
        output += std::format("File: {}\n", file.filename().string());
        output += std::format("```{}\n", infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
        output += "```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
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
