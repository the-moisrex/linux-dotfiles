#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_tests(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output;
    output += "Review this code and identify the highest-value missing tests.\n";
    output += "Focus on edge cases, regressions, error handling, boundary conditions, invalid "
              "input, and behavior that looks easy to break.\n";
    output += "Propose a minimal but effective test plan first, then provide a git diff that adds "
              "or updates the tests.\n";
    output += "Prefer the smallest practical diff that materially improves confidence.\n\n";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n", file.filename().string());
        output += std::format("```{}\n", infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
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
    os << R"EOF(Usage: prompt tests [--head N] [FILE...]
       some-command | prompt tests [--head N] [FILE...]

Review this code and identify the highest-value missing tests.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "tests", "Review this code and identify the highest-value missing tests.", &execute_tests, &render_help_tests);

} // namespace prompt::prompts
