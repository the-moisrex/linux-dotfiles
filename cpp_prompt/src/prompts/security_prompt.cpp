#include "prompt/prompts/prompts.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_output execute_security(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Review this for security and safety issues.\n";
    output += "Look for vulnerabilities, unsafe defaults, trust boundary mistakes, injection "
              "risks, missing validation, secrets exposure, privilege problems, dangerous file or "
              "process handling, and denial-of-service risks.\n";
    output += "Also call out reliability or safety hazards that could cause data loss, corruption, "
              "crashes, or harmful behavior.\n";
    output += "List findings by severity, explain the risk briefly, and suggest the smallest "
              "effective fix for each one.\n";
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += std::format("File: {}\n\n", file.filename().string());
            output += std::format("```{}\n", infer_lang(file));
            output += trim_context_nl(read_file(file), head_lines);
            output += "\n```\n";
            output += "\n";
        } else {
            error += std::format("Warning: File '{}' not found or is not a regular file.\n", std::string(ctx.args[i]));
        }
    }

    return {std::move(output), std::move(error), false, false};
}

void render_help_security(std::ostream& os) {
    os << R"EOF(Usage: prompt security [--head N] [FILE...]
       some-command | prompt security [--head N] [FILE...]

Review this for security and safety issues.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
