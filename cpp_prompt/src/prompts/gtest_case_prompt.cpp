#include "prompt/prompts/gtest_case_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_gtest_case(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines, false); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    bool exact = false;
    std::vector<std::string> test_names;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
        } else if (ctx.args[i] == "--exact") {
            exact = true;
        } else {
            test_names.emplace_back(ctx.args[i]);
        }
    }

    // gtest-case.sh embeds stdin before it validates the test names, so a
    // usage error still ships the piped context on stdout.
    std::string output;
    if (ctx.stdin_consumed) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }

    if (test_names.empty()) {
        return {std::move(output), 2, false,
                "Usage: prompt gtest-case [--head N] [--exact] <test-name> [test-name...]\n", ctx.stdin_consumed};
    }

    std::string tool = prompt::fs::bin_tool("gtest-case", ctx.exe_path, ctx.git_root).string();

    std::vector<std::string> cmd;
    cmd.push_back(tool);
    if (exact) cmd.push_back("--exact");
    for (auto const& name : test_names) cmd.push_back(name);

    std::vector<char const*> argv;
    argv.reserve(cmd.size() + 1);
    for (auto const& s : cmd) argv.push_back(s.c_str());
    argv.push_back(nullptr);

    auto result = prompt::process::run_command(argv, ctx.stdin_content);

    std::string tests;
    for (std::size_t i = 0; i < test_names.size(); ++i) {
        if (i) tests += ' ';
        tests += test_names[i];
    }

    output += "Additional Google Test case context:\n\n";
    output += "Tests: " + tests + "\n\n";
    output += "```cpp\n";
    output += trim_context_nl(result.stdout_data, head_lines);
    output += "\n```\n\n";
    output += "Use the test case context above when analyzing the issue.";

    return {std::move(output), 0, false, std::move(result.stderr_data), ctx.stdin_consumed};
}

void render_help_gtest_case(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt gtest-case [--head N] [--exact] <test-name> [test-name...]

Builds a debugging prompt and embeds the original source for Google Test cases.
By default, test names are prefix matches.

Options:
  --head N   Keep only the first N lines of each test case
  --exact    Match each given test name exactly
)EOF";
}

} // namespace prompt::prompts
