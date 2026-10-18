#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_tests(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output = R"PROMPT(Review this code and identify the highest-value missing tests.
Focus on edge cases, regressions, error handling, boundary conditions, invalid input, and behavior that looks easy to break.
Propose a minimal but effective test plan first, then provide a git diff that adds or updates the tests.
Prefer the smallest practical diff that materially improves confidence.

)PROMPT";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n";
    }
    for (auto arg : sources.missing) {
        error += "Warning: File '" + std::string(arg) +
                 "' not found or is not a "
                 "regular file.\n";
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_tests(std::ostream& os) {
    os << file_usage_help("tests") << R"EOF(
Review this code and identify the highest-value missing tests.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "tests", "Review this code and identify the highest-value missing tests.", &execute_tests, &render_help_tests);

} // namespace prompt::prompts
