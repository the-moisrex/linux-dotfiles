#include "prompt/prompts/prompts.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_output execute_migrate(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "You are a code migration expert.\n";
    output += "Help migrate the provided code from its current state to the target framework, API, or "
              "language version.\n";
    output += "\n";
    output += "Approach:\n";
    output += "1. Identify the current framework/API/version from the code.\n";
    output += "2. Identify the target from the task description or infer from context.\n";
    output += "3. List the breaking changes and deprecations that affect this code.\n";
    output += "4. Provide the migrated code as a git diff, preserving behavior.\n";
    output += "5. Note any manual steps required (config changes, dependency updates, data "
              "migrations).\n";
    output += "\n";
    output += "Guidelines:\n";
    output += "- Preserve existing behavior unless explicitly asked to change it\n";
    output += "- Make the smallest safe changes necessary\n";
    output += "- Prefer mechanical transformations that can be verified\n";
    output += "- Flag any semantic changes that cannot be done automatically\n";
    output += "- Update imports, function signatures, and deprecated patterns\n";
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
            // migrate.sh leaves a blank line before the closing fence.
            output += "\n```\n";
            output += "\n";
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    return {std::move(output), std::move(error), false, false};
}

void render_help_migrate(std::ostream& os) {
    os << R"EOF(Usage: prompt migrate [--head N] [FILE...]
       echo "migration task" | prompt migrate [--head N] [FILE...]

Code migration assistance prompt.
Helps migrate code between frameworks, APIs, language versions, or patterns.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
