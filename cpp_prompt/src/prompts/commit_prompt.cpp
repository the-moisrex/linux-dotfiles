#include "prompt/prompts/commit_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

namespace {

struct basic_commit_prompt_config {
    std::size_t head_lines = 0;
};

basic_commit_prompt_config parse_commit_args(std::span<std::string_view const> args) noexcept {
    basic_commit_prompt_config config;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head" && i + 1 < args.size()) {
            config.head_lines = std::stoull(std::string(args[++i]));
        }
    }
    return config;
}

} // namespace

prompt_result execute_commit(prompt_context&& ctx) noexcept {
    auto config = parse_commit_args({ctx.args.data(), ctx.args_count});
    
    std::vector<char const*> argv = {"git", "diff", "--no-color", nullptr};
    auto diff_result = prompt::process::run_command(argv);
    
    std::vector<char const*> argv_staged = {"git", "diff", "--cached", "--no-color", nullptr};
    auto staged_result = prompt::process::run_command(argv_staged);
    
    std::string combined_diff = staged_result.stdout_data;
    if (!combined_diff.empty() && !diff_result.stdout_data.empty()) combined_diff += "\n";
    combined_diff += diff_result.stdout_data;
    
    if (combined_diff.empty()) {
        return {"prompt commit: no changes to commit\n", 1, false};
    }
    
    std::string output;
    output += "Generate a Conventional Commit message from the following git diff.\n";
    output += "Format: <type>(<scope>): <subject>\n\n";
    output += "Types: feat, fix, docs, style, refactor, perf, test, chore, build, ci\n";
    output += "Keep subject under 50 chars. Body explains what and why, not how.\n\n";
    output += "```diff\n";
    output += trim_context(combined_diff, config.head_lines);
    output += "\n```\n";
    
    return {std::move(output), 0, false};
}

void render_help_commit(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt commit [--head N]

Generate a Conventional Commit message from staged/unstaged git diff.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts