#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

std::string git_output(std::initializer_list<char const*> args) {
    std::vector<char const*> argv;
    argv.reserve(args.size() + 1);
    for (char const* arg : args) argv.push_back(arg);
    argv.push_back(nullptr);
    auto result = prompt::process::run_command(argv);
    std::string out = std::move(result.stdout_data);
    while (!out.empty() && out.back() == '\n') out.pop_back(); // $(...) strips them
    return out;
}

} // namespace

prompt_output execute_commit(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    if (prompt::process::run_command(std::vector<std::string>{"git", "rev-parse", "--is-inside-work-tree"}).exit_code !=
        0) {
        throw soft_error{"prompt commit: Error: Not inside a git repository.\n"};
    }

    std::string diff_type = "staged changes";
    std::string diff = git_output({"git", "diff", "--cached", nullptr});
    if (diff.empty()) {
        diff_type = "unstaged changes";
        diff = git_output({"git", "diff", nullptr});
    }
    if (diff.empty()) {
        throw soft_error{"prompt commit: No changes found in the repository.\n"};
    }

    std::string output;
    output += std::format("Write a clear, descriptive Git commit message for the following {}.\n", diff_type);
    output += "Follow the Conventional Commits specification (e.g., feat:, fix:, docs:, "
              "refactor:, chore:).\n";
    output += "Keep the subject line concise (under 50 characters) and include a more detailed "
              "body if the changes are complex.\n";
    output += "Provide the commit message directly, without extra conversational text.\n\nProvide:\n1. A concise "
              "subject line.\n2. A short body explaining what changed and why.\n3. If helpful, an alternative subject "
              "line.\nKeep it specific and practical, not generic.\n\n\n```diff\n";
    output += trim_context_nl(diff, head_lines);
    output += "\n```\n\n";

    return {.text = std::move(output), .error = std::string{}, .no_clipboard = false, .stdin_consumed = false};
}

void render_help_commit(std::ostream& os) {
    os << R"EOF(Usage: prompt commit [--head N]

Asks the AI to write a Git commit message based on current changes.
It prioritizes staged changes (git diff --cached). If no changes are staged,
it evaluates all unstaged changes (git diff).

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
