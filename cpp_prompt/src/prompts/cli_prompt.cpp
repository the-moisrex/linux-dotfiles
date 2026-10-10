#include "prompt/prompts/cli_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/legacy/legacy_runner.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_cli(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::vector<std::string_view> cmd_args;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        cmd_args.push_back(ctx.args[i]);
    }

    if (cmd_args.empty()) {
        return {std::string{}, 1, false, "prompt cli: no command provided\n" + legacy::help_text("cli")};
    }

    // cli.sh: cmd_description="${ARGS[*]}" and eval "${ARGS[@]}" 2>&1 — eval
    // joins its arguments with spaces, so a single joined string is faithful.
    std::string command;
    for (std::size_t i = 0; i < cmd_args.size(); ++i) {
        if (i) command += ' ';
        command += cmd_args[i];
    }

    std::vector<char const*> argv = {"bash", "-c", "eval \"$0\" 2>&1", command.c_str(), nullptr};
    auto result = prompt::process::run_command(argv, ctx.stdin_content);

    std::string output;
    output += "Output of command `" + command + "`:\n\n";
    if (result.exit_code != 0) {
        output += "(Exited with status: " + std::to_string(result.exit_code) + ")\n\n";
    }
    output += "\n```text\n";
    if (!result.stdout_data.empty()) {
        output += trim_context_nl(result.stdout_data, head_lines);
    }
    output += "\n```\n";

    return {std::move(output), 0, false, std::string{}};
}

void render_help_cli(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt cli [--head N] <command> [args...]
       some-input | prompt cli [--head N] <command> [args...]

Executes the given CLI command and appends its output as a Markdown code block.
Useful for appending the output of arbitrary commands to your prompt chain.

Options:
  --head N   Keep only the first N lines of the command output
)EOF";
}

} // namespace prompt::prompts
