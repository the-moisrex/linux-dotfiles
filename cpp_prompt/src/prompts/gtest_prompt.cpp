#include "prompt/prompts/gtest_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_gtest_prompt_config {
    std::size_t head_lines = 0;
    std::vector<std::string_view> test_names;
};

basic_gtest_prompt_config parse_gtest_args(std::span<std::string_view const> args) noexcept {
    basic_gtest_prompt_config config;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head" && i + 1 < args.size()) {
            config.head_lines = std::stoull(std::string(args[++i]));
        } else {
            config.test_names.push_back(args[i]);
        }
    }
    return config;
}

} // namespace

prompt_result execute_gtest(prompt_context&& ctx) noexcept {
    auto config = parse_gtest_args({ctx.args.data(), ctx.args_count});
    
    if (config.test_names.empty()) {
        return {"prompt gtest: no test names specified\n", 2, false};
    }
    
    std::vector<std::string> cmd = {"gtest-case"};
    for (auto name : config.test_names) {
        cmd.push_back(std::string(name));
    }
    
    std::vector<char const*> argv;
    argv.reserve(cmd.size() + 1);
    for (auto const& s : cmd) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    
    auto result = prompt::process::run_command(argv, ctx.stdin_content);
    
    std::string output;
    output += "Google Test case source for analysis:\n\n";
    output += "```cpp\n";
    output += trim_context(result.stdout_data, config.head_lines);
    output += "\n```\n";
    output += "Exit code: " + std::to_string(result.exit_code) + "\n";
    
    return {std::move(output), result.exit_code, false};
}

void render_help_gtest(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt gtest [--head N] <TEST_NAME>...

Find Google Test test case source from names and embed for analysis.

Options:
  --head N   Keep only the first N lines of each embedded test
)EOF";
}

} // namespace prompt::prompts