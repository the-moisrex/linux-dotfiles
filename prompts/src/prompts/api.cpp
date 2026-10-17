#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_api(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output;
    output += "Review this API design.\n";
    output += "Look for confusing names, inconsistent behavior, unclear contracts, weak validation, "
              "awkward call sites, leaky abstractions, and backward-compatibility risks.\n";
    output += "Suggest the smallest meaningful API improvements, explain the tradeoffs briefly, and "
              "provide a git diff for the recommended changes.\n";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += embed_file_block(file, head_lines);
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_api(std::ostream& os) {
    os << R"EOF(Usage: prompt api [--head N] [FILE]...
       some-command | prompt api [--head N] [FILE]...

Review this API design.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("api", "Review this API design.", &execute_api, &render_help_api);

} // namespace prompt::prompts
