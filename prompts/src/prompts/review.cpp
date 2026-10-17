#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_review(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output = R"PROMPT(Review this code like a strong practical reviewer.
Prioritize bugs, behavioral regressions, risky assumptions, edge cases, maintainability problems, and missing tests.
List the most important findings first with short explanations, then provide a git diff for the most useful fixes.
If there are no major issues, say that clearly and still suggest a small safe improvement as a git diff if one is worthwhile.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
        output += "```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_review(std::ostream& os) {
    os << R"EOF(Usage: prompt review [--head N] [FILE...]
       some-command | prompt review [--head N] [FILE...]

Review this code like a strong practical reviewer.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("review", "Review this code like a strong practical reviewer.",
                                                &execute_review, &render_help_review);

} // namespace prompt::prompts
