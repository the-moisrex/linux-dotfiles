#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_fix(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output = R"PROMPT(Find the root problem here and propose the smallest useful fix.
Explain the issue briefly, mention any important assumptions, and provide the answer primarily as a git diff that can be applied directly.
Prefer minimal, surgical changes over broad rewrites.

)PROMPT";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
        output += "\n```\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_fix(std::ostream& os) {
    os << R"EOF(Usage: prompt fix [--head N] [FILE...]
       some-command | prompt fix [--head N] [FILE...]

Find the root problem here and propose the smallest useful fix.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "fix", "Find the root problem here and propose the smallest useful fix.", &execute_fix, &render_help_fix);

} // namespace prompt::prompts
