#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <ostream>
#include <string>

namespace prompt::prompts {

static void execute_cli_patch([[maybe_unused]] view const& v, result& r) {
    r.out =
        R"PROMPT(Write a complete, paste-ready CLI command or script that applies the requested changes directly to this repository.

An incoming patch or git diff describes the intended changes; do not use git apply, patch, or another patch-application command. Instead, edit the affected files in place. Output one fenced bash block containing the complete command or script and useful verification commands. Do not claim to have run it.

Preserve unrelated content. For edits to existing files, check the expected text or other precise anchors before writing, and fail with a clear message if they do not match. Check necessary preconditions before the first write where practical, and make rerunning the command harmless where practical. For new files, include their complete contents. Use ordinary CLI tools or Python's standard library rather than unmentioned dependencies. Do not leave placeholders, TODOs, or manual editing steps.

If the supplied context is insufficient to specify a safe edit, say exactly what is missing instead of inventing file contents or changes.

)PROMPT";
}

static void render_help_cli_patch(std::ostream& os) {
    os << file_usage_help(".cli.patch") << R"EOF(
Generate a paste-ready CLI script that applies repository changes directly instead of applying a patch or git diff.

Pipe in a change request or diff, and optionally supply files for the AI to
inspect when constructing the script.

Options:
)EOF" << file_options_help("the embedded files", true);
}

bool const registered = prompt::register_prompt(".cli.patch",
                                                "Generate a paste-ready CLI script that applies repository changes "
                                                "directly instead of applying a patch or git diff.",
                                                &execute_cli_patch, &render_help_cli_patch);

} // namespace prompt::prompts
