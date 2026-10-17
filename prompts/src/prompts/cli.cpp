#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace prompt::prompts {

static void render_help_cli(std::ostream& os);

static void execute_cli(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    auto cmd_args = positional_args(ctx);

    if (cmd_args.empty()) {
        std::string help_os = help_text(render_help_cli);
        throw soft_error{"prompt cli: no command provided\n" + help_os};
    }

    // cli.sh: cmd_description="${ARGS[*]}" and eval "${ARGS[@]}" 2>&1 — eval
    // joins its arguments with spaces, so a single joined string is faithful.
    std::string command;
    for (std::size_t i = 0; i < cmd_args.size(); ++i) {
        if (i != 0U) command += ' ';
        command += cmd_args[i];
    }

    std::vector<char const*> argv = {"bash", "-c", "eval \"$0\" 2>&1", command.c_str(), nullptr};
    auto result = prompt::process::run_command(argv, ctx.stdin_content);

    std::string output;
    output += std::format("Output of command `{}`:\n\n", command);
    if (result.exit_code != 0) {
        output += std::format("(Exited with status: {})\n\n", std::to_string(result.exit_code));
    }
    output += "\n```text\n";
    if (!result.stdout_data.empty()) {
        output += trim_context_nl(result.stdout_data, head_lines);
    }
    output += "\n```\n";

    ctx.out = std::move(output);
}

static void render_help_cli(std::ostream& os) {
    os << R"EOF(Usage: prompt cli [--head N] <command> [args...]
       some-input | prompt cli [--head N] <command> [args...]

Executes the given CLI command and appends its output as a Markdown code block.
Useful for appending the output of arbitrary commands to your prompt chain.

Options:
  --head N   Keep only the first N lines of the command output
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "cli",
    "Executes the given CLI command and appends its output as a Markdown code block. Useful for "
    "appending the output of arbitrary commands to your prompt chain.",
    &execute_cli, &render_help_cli);

} // namespace prompt::prompts
