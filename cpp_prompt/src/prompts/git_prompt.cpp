#include "prompt/prompts/git_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prompt::prompts {

namespace {

// Run `git <args>` in the inherited cwd (git.sh never uses -C / find_git_root
// for its own git calls, so the output is cwd-relative exactly like the script).
std::string git_raw(std::vector<std::string> const& args) noexcept {
    std::vector<char const*> argv;
    argv.reserve(args.size() + 1);
    for (auto const& s : args) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    return prompt::process::run_command(argv).stdout_data;
}

// ```bash echo "$(git branch --show-current ... || echo detached)"``` only
// substitutes 'detached' on a non-zero exit, so the exit code matters here.
std::string git_branch() noexcept {
    std::vector<char const*> argv = {"git", "branch", "--show-current", nullptr};
    auto result = prompt::process::run_command(argv);
    if (result.exit_code != 0) return "detached";
    std::string out = result.stdout_data;
    while (!out.empty() && out.back() == '\n') out.pop_back(); // $(...) strips them
    return out;
}

// Port of `git status --short | head -20` (direct to stdout, not captured):
// the first n complete \n-terminated lines, verbatim.
std::string head_lines_raw(std::string_view s, std::size_t n) noexcept {
    std::size_t pos = 0;
    std::size_t count = 0;
    while (pos < s.size() && count < n) {
        std::size_t nl = s.find('\n', pos);
        if (nl == std::string_view::npos) return std::string(s);
        pos = nl + 1;
        ++count;
    }
    return std::string(s.substr(0, pos));
}

} // namespace

prompt_result execute_git(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    // parse_arguments leaves the non-flag files in ARGS (with --head N removed).
    std::vector<std::string> files;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        files.emplace_back(ctx.args[i]);
    }

    std::string output;
    output += "You are a git expert. Help with the following git-related task.\n";
    output += "\n";
    output += "Provide:\n";
    output += "1. The exact git commands to run, in order.\n";
    output += "2. A brief explanation of what each command does and why.\n";
    output += "3. Common pitfalls or things that could go wrong at each step.\n";
    output += "4. If a task description is provided, implement exactly that.\n";
    output += "\n";
    output += "Common scenarios you can help with:\n";
    output += "- Merge conflict resolution strategies\n";
    output += "- Interactive rebase workflows\n";
    output += "- Bisecting to find problematic commits\n";
    output += "- Setting up git hooks and workflows\n";
    output += "- Recovering from mistakes (reset, reflog, cherry-pick)\n";
    output += "- Repository maintenance (gc, prune, shallow clone management)\n";
    output += "- Branch management and cleanup\n";
    output += "- Cherry-picking and conflict resolution\n";
    output += "\n";

    // Embed git status context if in a git repo.
    std::vector<char const*> rev = {"git", "rev-parse", "--is-inside-work-tree", nullptr};
    if (prompt::process::run_command(rev).exit_code == 0) {
        output += "Current git context:\n";
        output += "\n";
        output += "```text\n";
        output += "Branch: " + git_branch() + "\n";
        output += "Status:\n";
        output += head_lines_raw(git_raw({"git", "status", "--short"}), 20);
        output += "\n";
        output += "Recent commits:\n";
        output += git_raw({"git", "log", "--oneline", "-10"});
        output += "```\n";
        output += "\n";
    }

    // git.sh embeds files with a basename heading (no relative path, no fzf).
    std::string error;
    for (auto const& file : files) {
        std::filesystem::path path(file);
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec) && !ec) {
            std::string name = path.filename().string();
            std::string lang = infer_lang(path);
            std::string content = read_file(path);
            output += "File: " + name + "\n";
            output += "\n";
            output += "```" + lang + "\n";
            output += trim_context_nl(content, head_lines);
            output += "\n";
            output += "```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + file + "' not found.\n";
        }
    }

    if (!error.empty()) return {std::move(output), 0, false, std::move(error)};
    return {std::move(output), 0, false};
}

void render_help_git(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt git [--head N] [FILE...]
       echo "task description" | prompt git [--head N] [FILE...]

Git workflow assistance prompt.
Helps with branching strategies, merge conflicts, rebasing, bisecting, and other git operations.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
