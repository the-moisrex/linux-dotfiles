#include "prompt/prompts/spp_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_spp_prompt_config {
    std::size_t head_lines = 0;
    std::string symbol;
    std::vector<std::string_view> files;
};

basic_spp_prompt_config parse_spp_args(std::span<std::string_view const> args) noexcept {
    basic_spp_prompt_config config;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head" && i + 1 < args.size()) {
            config.head_lines = std::stoull(std::string(args[++i]));
        } else if (config.symbol.empty() && !args[i].starts_with('-')) {
            config.symbol = std::string(args[i]);
        } else {
            config.files.push_back(args[i]);
        }
    }
    return config;
}

} // namespace

prompt_result execute_spp(prompt_context&& ctx) noexcept {
    auto config = parse_spp_args({ctx.args.data(), ctx.args_count});
    
    if (config.symbol.empty()) {
        return {"prompt spp: no symbol specified\n", 2, false};
    }
    
    std::vector<std::string> cmd = {"spp", config.symbol};
    for (auto file_sv : config.files) {
        cmd.push_back(std::string(file_sv));
    }
    
    std::vector<char const*> argv;
    argv.reserve(cmd.size() + 1);
    for (auto const& s : cmd) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    
    auto result = prompt::process::run_command(argv, ctx.stdin_content);
    
    std::string output;
    output += "C++ symbol expansion for: " + config.symbol + "\n\n";
    output += "```cpp\n";
    output += trim_context(result.stdout_data, config.head_lines);
    output += "\n```\n";
    output += "Exit code: " + std::to_string(result.exit_code) + "\n";
    
    return {std::move(output), result.exit_code, false};
}

void render_help_spp(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt spp [--head N] <SYMBOL> [FILE...]

Extract full C++ function/class source using clang.
Reads .clang/.clangd for compile flags.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts