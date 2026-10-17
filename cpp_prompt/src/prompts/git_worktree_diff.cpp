#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

// bash captures with `2>/dev/null` — the tool's diagnostics never reach the
// user (git_raw is for `$(...)` of a command that may legitimately fail).
std::string git_raw(std::vector<std::string> const& args) {
    return prompt::process::command{args}.discard_stderr().run().stdout_data;
}

std::string strip_trailing_newlines(std::string_view s) {
    std::string out(s);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// Detect the base branch exactly like the script: upstream (@{u}), falling
// back to origin/main. `|| true` means a failing rev-parse yields "".
std::string detect_base_branch(std::string const& given) {
    if (!given.empty()) return given;
    std::string upstream =
        strip_trailing_newlines(git_raw(std::vector<std::string>{"git", "rev-parse", "--abbrev-ref", "@{u}"}));
    if (upstream.empty()) return "origin/main";
    return upstream;
}

} // namespace

static void execute_git_worktree_diff(prompt_context& ctx) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    if (prompt::process::run_command(std::vector<std::string>{"git", "rev-parse", "--is-inside-work-tree"}).exit_code !=
        0) {
        throw soft_error{"prompt git.worktree.diff: Error: Not inside a git repository.\n"};
    }

    // Custom flags: --not <commit>, --base <branch>; everything else is a file.
    std::vector<std::string> rest;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        rest.emplace_back(ctx.args[i]);
    }

    std::string base_branch;
    std::string not_commit;
    std::vector<std::string> files;
    for (std::size_t i = 0; i < rest.size();) {
        if (rest[i] == "--not") {
            if (i + 1 >= rest.size()) throw usage_error{"Missing value for --not\n"};
            not_commit = rest[i + 1];
            i += 2;
        } else if (rest[i] == "--base") {
            if (i + 1 >= rest.size()) throw usage_error{"Missing value for --base\n"};
            base_branch = rest[i + 1];
            i += 2;
        } else {
            files.push_back(rest[i]);
            ++i;
        }
    }

    base_branch = detect_base_branch(base_branch);

    std::vector<std::string> diff_args = {base_branch};
    if (!not_commit.empty()) {
        diff_args.emplace_back("--not");
        diff_args.push_back(not_commit);
    }

    std::vector<std::string> git_cmd = {"git", "diff"};
    for (auto const& a : diff_args) git_cmd.push_back(a);
    if (!files.empty()) {
        git_cmd.emplace_back("--");
        for (auto const& f : files) git_cmd.push_back(f);
    }

    std::string diff_output = strip_trailing_newlines(git_raw(git_cmd));
    if (!diff_output.empty()) {
        std::string output;
        output += std::format("Diffs ({}..HEAD):\n", base_branch);
        output += "\n```diff\n";
        output += trim_context_nl(diff_output, head_lines);
        output += "```\n";
        ctx.out = std::move(output);
        return;
    }
    ctx.out.clear();
    ctx.err = "No diffs found between current branch and " + base_branch + ".\n";
}

static void render_help_git_worktree_diff(std::ostream& os) {
    os << R"EOF(Usage: prompt git.worktree.diff [--not <commit>] [--base <branch>] [--head N] [FILE...]

Shows Git diffs for files changed since the upstream branch.
By default, compares against the upstream tracking branch (or origin/main).
Explicit FILE arguments restrict the diff to only those files.

Options:
  --not <commit>   Exclude a commit (and its ancestors) from the diff
  --base <branch>  Base branch to compare against (default: upstream or origin/main)
  --head N         Keep only the first N lines of embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "git.worktree.diff",
    "Shows Git diffs for files changed since the upstream branch. By default, compares against the "
    "upstream tracking branch (or origin/main). Explicit FILE arguments restrict the diff to only "
    "those files.",
    &execute_git_worktree_diff, &render_help_git_worktree_diff);

} // namespace prompt::prompts
