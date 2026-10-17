#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <span>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

// git -C <root> ls-files -- <patterns...> 2>/dev/null, non-empty lines only.
std::vector<std::string> git_ls_files(prompt_context const& ctx, std::vector<std::string> const& patterns) {
    std::vector<std::string> storage = {"bash", "-c", R"(git -C "$0" ls-files -- "$@" 2>/dev/null)"};
    storage.push_back(ctx.git_root.string());
    for (auto const& p : patterns) storage.push_back(p);

    auto result = prompt::process::run_command(storage);
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= result.stdout_data.size()) {
        std::size_t end = result.stdout_data.find('\n', start);
        if (end == std::string::npos) {
            if (start < result.stdout_data.size()) lines.push_back(result.stdout_data.substr(start));
            break;
        }
        if (end > start) lines.push_back(result.stdout_data.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

bool contains_ci_path(std::string const& line) {
    static std::string const needle = ".github/workflows/";
    std::string lower;
    lower.reserve(line.size());
    for (char c : line) lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return lower.contains(needle);
}

} // namespace

static void execute_ci(prompt_context& ctx) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output +=
        "You are a CI/CD pipeline expert.\nReview the provided pipeline configurations for correctness, security, and "
        "efficiency.\n\nFocus on:\n- Security: secrets handling, GITHUB_TOKEN scope, artifact permissions\n- "
        "Reliability: flaky test handling, retry logic, timeout configuration\n- Speed: parallel jobs, caching "
        "strategies, dependency optimization\n- Maintainability: reusable workflows, matrix builds, clear job "
        "naming\n- Best practices: pinned action versions, branch protection, status checks\n- Cost: unused jobs, "
        "excessive runner minutes, artifact retention\n\nProvide specific improvements as a git diff.\n\n";

    std::vector<std::string> ci_files;
    if (!ctx.git_root.empty()) {
        auto append = [&](std::span<const std::string> lines) {
            for (auto const& f : lines) ci_files.push_back(ctx.git_root.string() + "/" + f);
        };
        for (auto const& f : git_ls_files(ctx, std::vector<std::string>{"*.yml", "*.yaml"})) {
            if (contains_ci_path(f)) ci_files.push_back(ctx.git_root.string() + "/" + f);
        }
        append(git_ls_files(ctx, std::vector<std::string>{
                                     ".gitlab-ci.yml",
                                     ".gitlab-ci.yaml",
                                     "**/.gitlab-ci.yml",
                                     "**/.gitlab-ci.yaml",
                                 }));
        append(git_ls_files(ctx, std::vector<std::string>{"*Jenkinsfile*"}));
        append(git_ls_files(
            ctx, std::vector<std::string>{"*Dockerfile*", "*dockerfile*", "*docker-compose*", "*compose.y*"}));
        append(git_ls_files(ctx, std::vector<std::string>{".circleci/config.yml", "**/.circleci/config.yml"}));
        append(git_ls_files(ctx, std::vector<std::string>{".travis.yml", "**/.travis.yml"}));
    }

    if (ci_files.empty()) {
        output += "No CI/CD configuration files found in this repository.\nSearched for: GitHub Actions, GitLab CI, "
                  "Jenkins, Docker, CircleCI, Travis CI.\n";
        ctx.out = std::move(output);
        return;
    }

    std::vector<std::string> seen;
    for (auto const& file : ci_files) {
        bool dup = false;
        for (auto const& s : seen) {
            if (s == file) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        seen.push_back(file);

        std::error_code ec;
        if (std::filesystem::is_regular_file(file, ec)) {
            std::filesystem::path p(file);
            auto label = fs::relative_path(p, ctx.git_root.empty() ? std::nullopt : std::optional(ctx.git_root));
            output += std::format("File: {}\n\n", label.string());
            output += std::format("```{}\n", infer_lang(p));
            output += trim_context_nl(read_file(p), head_lines);
            output += "\n```\n\n";
        }
    }

    ctx.out = std::move(output);
}

static void render_help_ci(std::ostream& os) {
    os << R"EOF(Usage: prompt ci [--head N]
       echo "task description" | prompt ci [--head N]

CI/CD pipeline review prompt.
Automatically finds and embeds all CI/CD configuration files from the repository
(GitHub Actions, GitLab CI, Jenkins, Docker, etc.).

Options:
  --head N   Keep only the first N lines of each embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("ci",
                            "CI/CD pipeline review prompt. Automatically finds and embeds all CI/CD configuration "
                            "files from the repository (GitHub Actions, GitLab CI, Jenkins, Docker, etc.).",
                            &execute_ci, &render_help_ci);

} // namespace prompt::prompts
