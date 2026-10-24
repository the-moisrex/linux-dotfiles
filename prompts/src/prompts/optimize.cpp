#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_optimize_prompt(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Act as an expert prompt engineer. Review the provided prompt(s) or instructions below.
Your goal is to optimize the input to create a clearer, more effective, and more robust prompt for an AI assistant.

Provide:
1. A brief critique of the original prompt (identifying ambiguities, missing context, or structural issues).
2. A checklist of the specific improvements made.
3. The completely rewritten, optimized prompt enclosed in a markdown code block so it can be easily copied.

Here is the input to optimize:

)PROMPT";

    std::string error;
    auto sources = collect_file_args(v);
    for (auto const& file : sources.files) {
        // optimize-prompt.sh puts no blank line between "File:" and the fence.
        output += std::format("File: {}\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_optimize_prompt(std::ostream& os) {
    os << file_usage_help("optimize-prompt", "[FILE...]", "echo \"my rough prompt\"") << R"EOF(
Ask the AI to analyze and improve an input prompt.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("optimize-prompt", "Ask the AI to analyze and improve an input prompt.",
                                                &execute_optimize_prompt, &render_help_optimize_prompt);

} // namespace prompt::prompts
