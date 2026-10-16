#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prompt::prompts {

namespace {

// Run `git <args>` in the inherited cwd (git.sh never uses -C / find_git_root
// for its own git calls, so the output is cwd-relative exactly like the script).
std::string git_raw(std::vector<std::string> const& args) { return prompt::process::run_command(args).stdout_data; }

// ```bash echo "$(git branch --show-current ... || echo detached)"``` only
// substitutes 'detached' on a non-zero exit, so the exit code matters here.
std::string git_branch() {
    auto result = prompt::process::run_command(std::vector<std::string>{"git", "branch", "--show-current"});
    if (result.exit_code != 0) return "detached";
    std::string out = result.stdout_data;
    while (!out.empty() && out.back() == '\n') out.pop_back(); // $(...) strips them
    return out;
}

// Port of `git status --short | head -20` (direct to stdout, not captured):
// the first n complete \n-terminated lines, verbatim.
std::string head_lines_raw(std::string_view s, std::size_t n) {
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

static void execute_git(prompt_context& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
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
    output += "You are a git expert. Help with the following git-related task.\n\nProvide:\n1. The exact git commands "
              "to run, in order.\n2. A brief explanation of what each command does and why.\n3. Common pitfalls or "
              "things that could go wrong at each step.\n4. If a task description is provided, implement exactly "
              "that.\n\nCommon scenarios you can help with:\n- Merge conflict resolution strategies\n- Interactive "
              "rebase workflows\n- Bisecting to find problematic commits\n- Setting up git hooks and workflows\n- "
              "Recovering from mistakes (reset, reflog, cherry-pick)\n- Repository maintenance (gc, prune, shallow "
              "clone management)\n- Branch management and cleanup\n- Cherry-picking and conflict resolution\n\n";

    // Embed git status context if in a git repo.
    std::vector<char const*> rev = {"git", "rev-parse", "--is-inside-work-tree", nullptr};
    if (prompt::process::run_command(std::vector<std::string>{"git", "rev-parse", "--is-inside-work-tree"}).exit_code ==
        0) {
        output += "Current git context:\n\n```text\n";
        output += std::format("Branch: {}\n", git_branch());
        output += "Status:\n";
        output += head_lines_raw(git_raw(std::vector<std::string>{"git", "status", "--short"}), 20);
        output += "\nRecent commits:\n";
        output += git_raw(std::vector<std::string>{"git", "log", "--oneline", "-10"});
        output += "```\n\n";
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
            output += std::format("File: {}\n", name);
            output += '\n';
            output += std::format("```{}\n", lang);
            output += trim_context_nl(content, head_lines);
            output += "\n```\n\n";
        } else {
            error += std::format("Warning: File '{}' not found.\n", file);
        }
    }

    if (!error.empty()) {
        ctx.err = std::move(error);
    }
    ctx.out = std::move(output);
}

static void render_help_git(std::ostream& os) {
    os << R"EOF(Usage: prompt git [--head N] [FILE...]
       echo "task description" | prompt git [--head N] [FILE...]

Git workflow assistance prompt.
Helps with branching strategies, merge conflicts, rebasing, bisecting, and other git operations.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "git",
    "Git workflow assistance prompt. Helps with branching strategies, merge conflicts, rebasing, "
    "bisecting, and other git operations.",
    &execute_git, &render_help_git);

} // namespace prompt::prompts
