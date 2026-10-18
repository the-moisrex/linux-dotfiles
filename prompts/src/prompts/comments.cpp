#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_comments(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output = R"PROMPT(Improve the comments and inline documentation here.
Add only high-value comments, docstrings, or usage notes where they genuinely help understanding.
Avoid noisy commentary. Explain tricky behavior, assumptions, contracts, and non-obvious reasoning.
Provide the result as a git diff.

)PROMPT";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File not found: {}\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_comments(std::ostream& os) {
    os << file_usage_help("comments") << R"EOF(
Improve the comments and inline documentation here.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("comments", "Improve the comments and inline documentation here.",
                                                &execute_comments, &render_help_comments);

} // namespace prompt::prompts
