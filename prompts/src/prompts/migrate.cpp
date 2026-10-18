#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_migrate(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output = R"PROMPT(You are a code migration expert.
Help migrate the provided code from its current state to the target framework, API, or language version.

Approach:
1. Identify the current framework/API/version from the code.
2. Identify the target from the task description or infer from context.
3. List the breaking changes and deprecations that affect this code.
4. Provide the migrated code as a git diff, preserving behavior.
5. Note any manual steps required (config changes, dependency updates, data migrations).
)PROMPT";
    output +=
        "\nGuidelines:\n- Preserve existing behavior unless explicitly asked to change it\n- Make the smallest safe "
        "changes necessary\n- Prefer mechanical transformations that can be verified\n- Flag any semantic changes that "
        "cannot be done automatically\n- Update imports, function signatures, and deprecated patterns\n\n";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        // migrate.sh leaves a blank line before the closing fence.
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_migrate(std::ostream& os) {
    os << file_usage_help("migrate", "[FILE...]", "echo \"migration task\"") << R"EOF(
Code migration assistance prompt.
Helps migrate code between frameworks, APIs, language versions, or patterns.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("migrate",
                            "Code migration assistance prompt. Helps migrate code between frameworks, "
                            "APIs, language versions, or patterns.",
                            &execute_migrate, &render_help_migrate);

} // namespace prompt::prompts
