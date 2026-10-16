#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_migrate(prompt_context& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "You are a code migration expert.\n";
    output += "Help migrate the provided code from its current state to the target framework, API, or "
              "language version.\n";
    output += "\nApproach:\n1. Identify the current framework/API/version from the code.\n2. Identify the target from "
              "the task description or infer from context.\n3. List the breaking changes and deprecations that affect "
              "this code.\n4. Provide the migrated code as a git diff, preserving behavior.\n";
    output += "5. Note any manual steps required (config changes, dependency updates, data "
              "migrations).\n";
    output +=
        "\nGuidelines:\n- Preserve existing behavior unless explicitly asked to change it\n- Make the smallest safe "
        "changes necessary\n- Prefer mechanical transformations that can be verified\n- Flag any semantic changes that "
        "cannot be done automatically\n- Update imports, function signatures, and deprecated patterns\n\n";

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
            output += "\n```\n\n";
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_migrate(std::ostream& os) {
    os << R"EOF(Usage: prompt migrate [--head N] [FILE...]
       echo "migration task" | prompt migrate [--head N] [FILE...]

Code migration assistance prompt.
Helps migrate code between frameworks, APIs, language versions, or patterns.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("migrate",
                            "Code migration assistance prompt. Helps migrate code between frameworks, "
                            "APIs, language versions, or patterns.",
                            &execute_migrate, &render_help_migrate);

} // namespace prompt::prompts
