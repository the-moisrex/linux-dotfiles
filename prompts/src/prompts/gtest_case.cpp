#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_gtest_case(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx, false);

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
        throw usage_error{"Usage: prompt gtest-case [--head N] [--exact] <test-name> [test-name...]\n",
                          std::move(output)};
    }

    std::string tool = prompt::fs::bin_tool("gtest-case", ctx.exe_path, ctx.git_root).string();

    std::vector<std::string> cmd;
    cmd.push_back(tool);
    if (exact) cmd.emplace_back("--exact");
    for (auto const& name : test_names) cmd.push_back(name);

    auto result = prompt::process::run_command(cmd, ctx.stdin_content);

    std::string tests;
    for (std::size_t i = 0; i < test_names.size(); ++i) {
        if (i != 0U) tests += ' ';
        tests += test_names[i];
    }

    output += "Additional Google Test case context:\n\n";
    output += std::format("Tests: {}\n\n", tests);
    output += "```cpp\n";
    output += trim_context_nl(result.stdout_data, head_lines);
    output += "\n```\n\nUse the test case context above when analyzing the issue.";

    ctx.out = std::move(output);
    ctx.err = std::move(result.stderr_data);
}

static void render_help_gtest_case(std::ostream& os) {
    os << R"EOF(Usage: prompt gtest-case [--head N] [--exact] <test-name> [test-name...]

Builds a debugging prompt and embeds the original source for Google Test cases.
By default, test names are prefix matches.

Options:
  --head N   Keep only the first N lines of each test case
  --exact    Match each given test name exactly
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "gtest-case",
    "Builds a debugging prompt and embeds the original source for Google Test cases. By default, test "
    "names are prefix matches.",
    &execute_gtest_case, &render_help_gtest_case);

} // namespace prompt::prompts
