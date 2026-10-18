#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_api(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output = R"PROMPT(Review this API design.
Look for confusing names, inconsistent behavior, unclear contracts, weak validation, awkward call sites, leaky abstractions, and backward-compatibility risks.
Suggest the smallest meaningful API improvements, explain the tradeoffs briefly, and provide a git diff for the recommended changes.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += embed_file_block(file, opts);
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_api(std::ostream& os) {
    os << file_usage_help("api") << R"EOF(
Review this API design.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("api", "Review this API design.", &execute_api, &render_help_api);

} // namespace prompt::prompts
