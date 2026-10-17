#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

bool tool_exists(std::string const& name) {
    std::string cmd = "command -v " + name + " >/dev/null 2>&1";
    return prompt::process::run_command({"bash", "-c", cmd}).exit_code == 0;
}

std::string strip_trailing_newlines(std::string s) {
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// gh issue view <num> --json <field> -q <query> 2>/dev/null || true
std::string gh_issue_field(std::string const& number, std::string const& field, std::string const& query) {
    std::vector<std::string> storage = {
        "bash", "-c", R"(gh issue view "$0" --json "$1" -q "$2" 2>/dev/null)", number, field, query,
    };
    return strip_trailing_newlines(prompt::process::run_command(storage).stdout_data);
}

} // namespace

static void execute_gh_issue(prompt_context& ctx) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
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
                throw soft_error{std::move(error)};
            }
        }
    }

    std::string output;
    output += "Implement the following GitHub issue. Read the issue carefully, explore the\ncodebase to understand the "
              "current state, and produce a working implementation.\nFollow existing code conventions, patterns, and "
              "project structure.\n\n## Requirements\n\n- Read relevant source files before writing any code.\n- Make "
              "minimal, focused changes that solve the problem.\n- Preserve existing code style and conventions.\n- Do "
              "not add unnecessary abstractions or rewrites.\n- Return your changes as a git diff that can be applied "
              "directly.\n\n---\n\n";

    if (!issue_title.empty()) {
        output += std::format("# Issue #{}: {}\n\n", issue_number, issue_title);
        if (!issue_url.empty()) output += "URL: " + issue_url + "\n";
        if (!issue_labels.empty()) output += "Labels: " + issue_labels + "\n";
        output += '\n';
        if (!issue_body.empty()) output += issue_body + "\n";
        if (!issue_comments.empty()) {
            output += "\n## Comments\n\n";
            output += issue_comments;
            output += '\n';
        }
        output += '\n';
    } else if (!stdin_content.empty()) {
        if (!issue_number.empty()) {
            output += std::format("# Issue #{} (provided via stdin)\n", issue_number);
        } else {
            output += "# Issue Description (provided via stdin)\n";
        }
        output += '\n';
        output += stdin_content + "\n";
        output += '\n';
    } else {
        error += "No issue data available.\n";
        throw soft_error{std::move(error), std::move(output)};
    }

    ctx.out = std::move(output);
    ctx.err = error;
}

static void render_help_gh_issue(std::ostream& os) {
    os << R"EOF(Usage: prompt gh.issue <ISSUE_NUMBER>
       echo "issue context" | prompt gh.issue [ISSUE_NUMBER]

Fetch a GitHub issue and generate a prompt asking the AI to implement it.
Uses `gh issue view` to retrieve issue details. If `gh` is unavailable,
reads issue context from stdin or produces a generic implementation prompt.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("gh.issue",
                            "Fetch a GitHub issue and generate a prompt asking the AI to implement it. Uses `gh issue "
                            "view` to retrieve issue details. If `gh` is unavailable, reads issue context from stdin "
                            "or produces a generic implementation prompt.",
                            &execute_gh_issue, &render_help_gh_issue);

} // namespace prompt::prompts
