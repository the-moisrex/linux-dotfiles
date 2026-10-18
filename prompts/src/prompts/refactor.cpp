#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_refactor(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output = R"PROMPT(Refactor this while preserving behavior.
Focus on clarity, structure, duplication removal, naming, cohesion, and simplifying control flow without changing the intended behavior.
Briefly explain the refactor plan, then provide a git diff for the recommended changes.
Keep the diff as small and safe as possible.

)PROMPT";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        error += "Warning: File '" + std::string(arg) +
                 "' not found or is not a "
                 "regular file.\n";
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_refactor(std::ostream& os) {
    os << file_usage_help("refactor") << R"EOF(
Refactor this while preserving behavior.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("refactor", "Refactor this while preserving behavior.",
                                                &execute_refactor, &render_help_refactor);

} // namespace prompt::prompts
