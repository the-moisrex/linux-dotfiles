#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_security(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output = R"PROMPT(Review this for security and safety issues.
Look for vulnerabilities, unsafe defaults, trust boundary mistakes, injection risks, missing validation, secrets exposure, privilege problems, dangerous file or process handling, and denial-of-service risks.
Also call out reliability or safety hazards that could cause data loss, corruption, crashes, or harmful behavior.
List findings by severity, explain the risk briefly, and suggest the smallest effective fix for each one.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File '{}' not found or is not a regular file.\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_security(std::ostream& os) {
    os << R"EOF(Usage: prompt security [--head N] [FILE...]
       some-command | prompt security [--head N] [FILE...]

Review this for security and safety issues.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("security", "Review this for security and safety issues.",
                                                &execute_security, &render_help_security);

} // namespace prompt::prompts
