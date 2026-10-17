#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_explain(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output = R"PROMPT(Explain this clearly and concretely.
Describe what it does, how the parts fit together, key control flow, important assumptions, and likely failure points.
If useful, finish with a small git diff that improves readability through naming, structure, or comments.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += embed_file_block(file, head_lines);
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File not found or is not a regular file - {}\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_explain(std::ostream& os) {
    os << R"EOF(Usage: prompt explain [--head N] [FILE]...
       some-command | prompt explain [--head N] [FILE]...

Explain this clearly and concretely.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("explain", "Explain this clearly and concretely.", &execute_explain, &render_help_explain);

} // namespace prompt::prompts
