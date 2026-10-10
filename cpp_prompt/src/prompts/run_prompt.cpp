#include "prompt/prompts/run_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_run_prompt_config {
    std::size_t head_lines = 0;
    std::string target;
    std::vector<std::string_view> args;
};

basic_run_prompt_config parse_run_args(std::span<std::string_view const> args) noexcept {
    basic_run_prompt_config config;
    bool seen_target = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head" && i + 1 < args.size()) {
            // Handled by dispatcher
        } else if (!seen_target && !args[i].starts_with('-')) {
            config.target = std::string(args[i]);
            seen_target = true;
        } else {
            config.args.push_back(args[i]);
        }
    }
    return config;
}

} // namespace

prompt_result execute_run(prompt_context&& ctx) noexcept {
    auto config = parse_run_args({ctx.args.data(), ctx.args_count});
    
    if (config.target.empty()) {
        return {"prompt run: no target specified\n", 2, false};
    }
    
    std::vector<std::string> cmd = {"run", config.target};
    for (auto arg : config.args) {
        cmd.push_back(std::string(arg));
    }
    
    std::vector<char const*> argv;
    argv.reserve(cmd.size() + 1);
    for (auto const& s : cmd) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    
    auto result = prompt::process::run_command(argv, ctx.stdin_content);
    
    std::string output;
    output += "Build & run output for target: " + config.target + "\n\n";
    output += "```text\n";
    output += trim_context(result.stdout_data, ctx.head_lines);
    if (!result.stderr_data.empty()) {
        output += "\n--- STDERR ---\n";
        output += trim_context(result.stderr_data, ctx.head_lines);
    }
    output += "\n```\n";
    output += "Exit code: " + std::to_string(result.exit_code) + "\n";
    
    return {std::move(output), result.exit_code, false};
}

void render_help_run(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt run [--head N] <target> [args...]

Find git root, locate CMake build dir, build and run target, embed output.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts