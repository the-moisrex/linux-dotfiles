#include "../core/git.hpp"
#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <cstddef>
#include <format>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::prompts {

namespace {

struct commit_options {
    std::vector<std::string> exclude;
    std::vector<std::string> only;
    bool prompt_only = false;
};

commit_options parse_commit_options(std::vector<std::string> const& args) {
    commit_options opts;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view a = args[i];
        auto take_value = [&](std::string_view flag) -> std::optional<std::string_view> {
            if (a == flag) {
                if (i + 1 >= args.size())
                    throw soft_error{std::format("prompt commit: option '{}' requires a value.\n", flag)};
                return args[++i];
            }
            if (a.size() > flag.size() && a.starts_with(flag) && a[flag.size()] == '=')
                return a.substr(flag.size() + 1);
            return std::nullopt;
        };
        if (auto x = take_value("--exclude")) {
            opts.exclude.emplace_back(*x);
            continue;
        }
        if (auto x = take_value("--only")) {
            opts.only.emplace_back(*x);
            continue;
        }
        if (a == "--prompt-only") {
            opts.prompt_only = true;
            continue;
        }
        // Unknown args are ignored for backward compatibility.
    }
    return opts;
}

std::string_view commit_instructions() {
    return R"PROMPT(Follow the Conventional Commits specification (e.g., feat:, fix:, docs:, refactor:, chore:).
Keep the subject line concise (under 50 characters) and include a more detailed body if the changes are complex.
Provide the commit message directly, without extra conversational text.

Provide:
1. A concise subject line.
2. A short body explaining what changed and why.
Keep it specific and practical, not generic. Only return copyable commit message.
)PROMPT";
}

} // namespace

static void execute_commit(view const& v, result& r) {
    auto head_lines = head_lines_or_throw(v);
    auto opts = parse_commit_options(positional_args(v));

    // Prompt-only: emit just the instructions, skip git/context entirely.
    if (opts.prompt_only) {
        std::string output;
        output += "Write a clear, descriptive Git commit message for the current changes.\n";
        output += commit_instructions();
        r.out = std::move(output);
        return;
    }

    if (prompt::process::run_command(std::vector<std::string>{"git", "rev-parse", "--is-inside-work-tree"}).exit_code !=
        0) {
        throw soft_error{"prompt commit: Error: Not inside a git repository.\n"};
    }

    std::string diff_type = "staged changes";
    std::string diff = prompt::git::run({"git", "diff", "--cached"});

    if (diff.empty()) {
        diff_type = "unstaged and untracked changes";
        diff = prompt::git::run({"git", "diff"});

        // Run both operations from the root so subdirectory invocations
        // include all untracked files and resolve their paths consistently.
        auto root_result = prompt::process::run_command(std::vector<std::string>{"git", "rev-parse", "--show-cdup"});
        if (root_result.exit_code != 0) {
            throw soft_error{"prompt commit: Error: Could not locate repository root.\n"};
        }

        std::string root = std::move(root_result.stdout_data);
        while (!root.empty() && root.back() == '\n') root.pop_back();
        if (root.empty()) root = ".";

        // NUL-delimited names preserve newlines and other special characters.
        auto listing = prompt::process::run_command(
            std::vector<std::string>{"git", "-C", root, "ls-files", "--others", "--exclude-standard", "-z"});
        if (listing.exit_code != 0) {
            throw soft_error{"prompt commit: Error: Could not list untracked files.\n"};
        }

        auto const& names = listing.stdout_data;
        for (std::size_t pos = 0; pos < names.size();) {
            auto const end = names.find('\0', pos);
            if (end == std::string::npos) {
                throw soft_error{"prompt commit: Error: Invalid untracked-file list.\n"};
            }

            auto path = names.substr(pos, end - pos);
            pos = end + 1;
            if (path.empty()) continue;

            if (path.ends_with('/')) {
                throw soft_error{
                    std::format("prompt commit: Cannot diff untracked directory '{}'; stage it first.\n", path)};
            }

            // Let Git handle modes, symlinks, binary files, and newline markers.
            auto file_diff = prompt::process::run_command(
                std::vector<std::string>{"git", "-C", root, "diff", "--no-index", "--", "/dev/null", "./" + path});

            // --no-index returns 1 when it finds a difference.
            if (file_diff.exit_code != 0 && file_diff.exit_code != 1) {
                throw soft_error{std::format("prompt commit: Error: Could not diff untracked file '{}'.\n", path)};
            }

            if (!diff.empty() && diff.back() != '\n') diff.push_back('\n');
            diff += file_diff.stdout_data;
        }
    }

    if (!opts.exclude.empty() || !opts.only.empty()) {
        diff = prompt::git::filter_diff_by_path(diff, opts.only, opts.exclude);
    }

    if (diff.empty()) {
        throw soft_error{"prompt commit: No changes found in the repository.\n"};
    }

    std::string trimmed_diff = trim_context_nl(diff, head_lines);
    std::string numbered_diff = prompt::git::add_line_numbers_to_diff(trimmed_diff);

    std::string output;
    output += std::format("Write a clear, descriptive Git commit message for the following {}.\n", diff_type);
    output += commit_instructions();
    output += "```diff\n";
    output += numbered_diff;
    if (output.back() != '\n') output.push_back('\n');
    output += "```\n ";

    r.out = std::move(output);
}

static void render_help_commit(std::ostream& os) {
    os << R"EOF(Usage: prompt commit [--head N] [--exclude GLOB]... [--only GLOB]... [--prompt-only]

Asks the AI to write a Git commit message based on current changes.
It prioritizes staged changes (git diff --cached). If no changes are staged,
it evaluates all unstaged and untracked changes.

Options:
  --head N       Keep only the first N lines of the embedded context
  --exclude GLOB Skip files matching GLOB (repeatable; matched against path and basename)
  --only GLOB    Show only files matching GLOB (repeatable; alias: --file)
  --prompt-only  Emit only the prompt text, without any diff context
)EOF";
}

bool const registered = prompt::register_prompt(
    "commit",
    "Asks the AI to write a Git commit message based on current changes. It prioritizes staged "
    "changes (git diff --cached). If no changes are staged, it evaluates all unstaged and untracked changes.",
    &execute_commit, &render_help_commit);

} // namespace prompt::prompts
