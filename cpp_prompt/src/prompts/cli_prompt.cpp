#include "prompt/prompts/cli_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>

namespace prompt::prompts {

namespace {

struct basic_cli_prompt_config {
    std::size_t head_lines = 0;
    std::string command;
};

basic_cli_prompt_config parse_cli_args(std::span<std::string_view const> args) noexcept {
    basic_cli_prompt_config config;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head" && i + 1 < args.size()) {
            config.head_lines = std::stoull(std::string(args[++i]));
        } else if (config.command.empty() && !args[i].starts_with('-')) {
            for (std::size_t j = i; j < args.size(); ++j) {
                if (j > i) config.command += " ";
                config.command += args[j];
            }
            break;
        }
    }
    return config;
}

} // namespace

prompt_result execute_cli(prompt_context&& ctx) noexcept {
    auto config = parse_cli_args({ctx.args.data(), ctx.args_count});
    
    if (config.command.empty()) {
        return {"prompt cli: no command specified\n", 2, false};
    }
    
    std::vector<char const*> argv = {"sh", "-c", config.command.c_str(), nullptr};
    auto result = prompt::process::run_command(argv, ctx.stdin_content);
    
    std::string output;
    output += "Command: " + config.command + "\n\n";
    output += "```text\n";
    output += trim_context(result.stdout_data, config.head_lines);
    if (!result.stderr_data.empty()) {
        output += "\n--- STDERR ---\n";
        output += trim_context(result.stderr_data, config.head_lines);
    }
    output += "\n```\n";
    output += "Exit code: " + std::to_string(result.exit_code) + "\n";
    
    return {std::move(output), result.exit_code, false};
}

void render_help_cli(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt cli [--head N] <COMMAND>

Run a shell command and embed its output for AI context.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts