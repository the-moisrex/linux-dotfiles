#include "prompt/prompts/gtest_case_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

namespace {

struct basic_gtest_case_prompt_config {
    std::size_t head_lines = 0;
    std::string test_name;
};

basic_gtest_case_prompt_config parse_gtest_case_args(std::span<std::string_view const> args) noexcept {
    basic_gtest_case_prompt_config config;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head" && i + 1 < args.size()) {
            config.head_lines = std::stoull(std::string(args[++i]));
        } else if (config.test_name.empty() && !args[i].starts_with('-')) {
            config.test_name = std::string(args[i]);
        }
    }
    return config;
}

} // namespace

prompt_result execute_gtest_case(prompt_context&& ctx) noexcept {
    auto config = parse_gtest_case_args({ctx.args.data(), ctx.args_count});
    
    if (config.test_name.empty()) {
        return {"prompt gtest-case: no test name specified\n", 2, false};
    }
    
    std::vector<std::string> cmd = {"gtest-case", config.test_name};
    std::vector<char const*> argv;
    argv.reserve(cmd.size() + 1);
    for (auto const& s : cmd) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    
    auto result = prompt::process::run_command(argv, ctx.stdin_content);
    
    std::string output;
    output += "Google Test case: " + config.test_name + "\n\n";
    output += "```cpp\n";
    output += trim_context(result.stdout_data, config.head_lines);
    output += "\n```\n";
    output += "Exit code: " + std::to_string(result.exit_code) + "\n";
    
    return {std::move(output), result.exit_code, false};
}

void render_help_gtest_case(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt gtest-case [--head N] <TEST_NAME>

Find a single Google Test case source from its name.

Options:
  --head N   Keep only the first N lines of the embedded test
)EOF";
}

} // namespace prompt::prompts