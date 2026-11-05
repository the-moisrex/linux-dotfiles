#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <cstddef>
#include <format>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// Annotate ordinary unified diffs using destination-file line numbers.
// Leave combined diffs unchanged because they have multiple prefix columns.
std::string add_line_numbers_to_diff(std::string_view diff) {
    std::istringstream stream{std::string(diff)};
    std::string line;
    std::string result;

    unsigned long long new_line_num = 0;
    bool in_hunk = false;

    while (std::getline(stream, line)) {
        if (line.starts_with("diff ")) {
            in_hunk = false;
            result += line;
            result.push_back('\n');
            continue;
        }

        if (line.starts_with("@@")) {
            in_hunk = false;
            if (line.starts_with("@@ -")) {
                auto pos = line.find(" +");
                if (pos != std::string::npos) {
                    try {
                        new_line_num = std::stoull(line.substr(pos + 2));
                        in_hunk = true;
                    } catch (...) {
                        // Preserve malformed headers without annotation.
                    }
                }
            }
            result += line;
            result.push_back('\n');
            continue;
        }

        if (!in_hunk) {
            result += line;
            result.push_back('\n');
            continue;
        }
        if (line.empty()) {
            result += std::format("{:5d}   \n", new_line_num++);
            continue;
        }

        char prefix = line[0];
        if (prefix == '+') {
            result += std::format("{:5d} + {}", new_line_num++, line.substr(1));
        } else if (prefix == '-') {
            result += std::format("      - {}", line.substr(1));
        } else if (prefix == ' ') {
            result += std::format("{:5d}   {}", new_line_num++, line.substr(1));
        } else {
            // Diff metadata, such as "\ No newline at end of file".
            result += line;
        }
        result.push_back('\n');
    }

    return result;
}

} // namespace

static void execute_commit(view const& v, result& r) {
    auto head_lines = head_lines_or_throw(v);

    if (prompt::process::run_command(std::vector<std::string>{"git", "rev-parse", "--is-inside-work-tree"}).exit_code !=
        0) {
        throw soft_error{"prompt commit: Error: Not inside a git repository.\n"};
    }

    std::string diff_type = "staged changes";
    std::string diff = git_output({"git", "diff", "--cached"});

    if (diff.empty()) {
        diff_type = "unstaged and untracked changes";
        diff = git_output({"git", "diff"});

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

    if (diff.empty()) {
        throw soft_error{"prompt commit: No changes found in the repository.\n"};
    }

    std::string trimmed_diff = trim_context_nl(diff, head_lines);
    std::string numbered_diff = add_line_numbers_to_diff(trimmed_diff);

    std::string output;
    output += std::format("Write a clear, descriptive Git commit message for the following {}.\n", diff_type);
    output += R"PROMPT(Follow the Conventional Commits specification (e.g., feat:, fix:, docs:, refactor:, chore:).
Keep the subject line concise (under 50 characters) and include a more detailed body if the changes are complex.
Provide the commit message directly, without extra conversational text.

Provide:
1. A concise subject line.
2. A short body explaining what changed and why.
Keep it specific and practical, not generic. Only return copyable commit message.
```diff
)PROMPT";
    output += numbered_diff;
    if (output.back() != '\n') output.push_back('\n');
    output += "```\n ";

    r.out = std::move(output);
}

static void render_help_commit(std::ostream& os) {
    os << R"EOF(Usage: prompt commit [--head N]

Asks the AI to write a Git commit message based on current changes.
It prioritizes staged changes (git diff --cached). If no changes are staged,
it evaluates all unstaged and untracked changes.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

bool const registered = prompt::register_prompt(
    "commit",
    "Asks the AI to write a Git commit message based on current changes. It prioritizes staged "
    "changes (git diff --cached). If no changes are staged, it evaluates all unstaged and untracked changes.",
    &execute_commit, &render_help_commit);

} // namespace prompt::prompts
