#include "prompt/prompts/gh_issue_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

bool tool_exists(std::string const& name) noexcept {
    std::string cmd = "command -v " + name + " >/dev/null 2>&1";
    std::vector<char const*> argv = {"bash", "-c", cmd.c_str(), nullptr};
    return prompt::process::run_command(argv).exit_code == 0;
}

std::string strip_trailing_newlines(std::string s) noexcept {
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// gh issue view <num> --json <field> -q <query> 2>/dev/null || true
std::string gh_issue_field(std::string const& number, std::string const& field, std::string const& query) {
    std::vector<std::string> storage;
    storage.push_back("bash");
    storage.push_back("-c");
    storage.push_back("gh issue view \"$0\" --json \"$1\" -q \"$2\" 2>/dev/null");
    storage.push_back(number);
    storage.push_back(field);
    storage.push_back(query);
    std::vector<char const*> argv;
    argv.reserve(storage.size() + 1);
    for (auto const& s : storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    return strip_trailing_newlines(prompt::process::run_command(argv).stdout_data);
}

} // namespace

prompt_result execute_gh_issue(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    // init_prompt --no-files: --head is the only recognized flag; everything
    // else is treated as the issue number.
    std::vector<std::string> rest;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        rest.emplace_back(ctx.args[i]);
    }

    std::string issue_number = rest.empty() ? std::string{} : rest.front();
    std::string issue_url;
    std::string issue_title;
    std::string issue_body;
    std::string issue_labels;
    std::string issue_comments;
    std::string stdin_content = strip_trailing_newlines(ctx.stdin_content);
    std::string error;

    if (!issue_number.empty()) {
        if (tool_exists("gh")) {
            issue_url = gh_issue_field(issue_number, "url", ".url");
            issue_title = gh_issue_field(issue_number, "title", ".title");
            issue_body = gh_issue_field(issue_number, "body", ".body");
            issue_labels = gh_issue_field(issue_number, "labels", "[.[].name]");
            issue_comments = gh_issue_field(
                issue_number, "comments", R"raw(.comments | map("**\(.author.login)**: \(.body)") | join("\n\n"))raw");
        } else {
            error += "prompt gh.issue: Warning: 'gh' CLI is not installed. Falling back to stdin.\n";
            if (stdin_content.empty()) {
                error += "prompt gh.issue: No issue data available. Pipe issue details or install gh CLI.\n";
                return {std::string{}, 1, false, error};
            }
        }
    }

    std::string output;
    output += "Implement the following GitHub issue. Read the issue carefully, explore the\n";
    output += "codebase to understand the current state, and produce a working implementation.\n";
    output += "Follow existing code conventions, patterns, and project structure.\n";
    output += "\n";
    output += "## Requirements\n";
    output += "\n";
    output += "- Read relevant source files before writing any code.\n";
    output += "- Make minimal, focused changes that solve the problem.\n";
    output += "- Preserve existing code style and conventions.\n";
    output += "- Do not add unnecessary abstractions or rewrites.\n";
    output += "- Return your changes as a git diff that can be applied directly.\n";
    output += "\n";
    output += "---\n";
    output += "\n";

    if (!issue_title.empty()) {
        output += "# Issue #" + issue_number + ": " + issue_title + "\n\n";
        if (!issue_url.empty()) output += "URL: " + issue_url + "\n";
        if (!issue_labels.empty()) output += "Labels: " + issue_labels + "\n";
        output += "\n";
        if (!issue_body.empty()) output += issue_body + "\n";
        if (!issue_comments.empty()) {
            output += "\n## Comments\n\n";
            output += issue_comments;
            output += "\n";
        }
        output += "\n";
    } else if (!stdin_content.empty()) {
        if (!issue_number.empty()) {
            output += "# Issue #" + issue_number + " (provided via stdin)\n";
        } else {
            output += "# Issue Description (provided via stdin)\n";
        }
        output += "\n";
        output += stdin_content + "\n";
        output += "\n";
    } else {
        error += "No issue data available.\n";
        return {std::move(output), 1, false, error};
    }

    return {std::move(output), 0, false, error, ctx.stdin_consumed};
}

void render_help_gh_issue(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt gh.issue <ISSUE_NUMBER>
       echo "issue context" | prompt gh.issue [ISSUE_NUMBER]

Fetch a GitHub issue and generate a prompt asking the AI to implement it.
Uses `gh issue view` to retrieve issue details. If `gh` is unavailable,
reads issue context from stdin or produces a generic implementation prompt.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
