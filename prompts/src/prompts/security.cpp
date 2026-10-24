#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_security(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Review this for security and safety issues.
Look for vulnerabilities, unsafe defaults, trust boundary mistakes, injection risks, missing validation, secrets exposure, privilege problems, dangerous file or process handling, and denial-of-service risks.
Also call out reliability or safety hazards that could cause data loss, corruption, crashes, or harmful behavior.
List findings by severity, explain the risk briefly, and suggest the smallest effective fix for each one.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(v);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File '{}' not found or is not a regular file.\n", std::string(arg));
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_security(std::ostream& os) {
    os << file_usage_help("security") << R"EOF(
Review this for security and safety issues.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("security", "Review this for security and safety issues.",
                                                &execute_security, &render_help_security);

} // namespace prompt::prompts
