#include "prompt/prompts/ci_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

// git -C <root> ls-files -- <patterns...> 2>/dev/null, non-empty lines only.
std::vector<std::string> git_ls_files(prompt_context const& ctx, std::vector<std::string> const& patterns) {
    std::vector<std::string> storage;
    storage.push_back("bash");
    storage.push_back("-c");
    storage.push_back("git -C \"$0\" ls-files -- \"$@\" 2>/dev/null");
    storage.push_back(ctx.git_root.string());
    for (auto const& p : patterns) storage.push_back(p);

    std::vector<char const*> argv;
    argv.reserve(storage.size() + 1);
    for (auto const& s : storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);

    auto result = prompt::process::run_command(argv);
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
    return lower.find(needle) != std::string::npos;
}

} // namespace

prompt_result execute_ci(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "You are a CI/CD pipeline expert.\n";
    output += "Review the provided pipeline configurations for correctness, security, and efficiency.\n";
    output += "\n";
    output += "Focus on:\n";
    output += "- Security: secrets handling, GITHUB_TOKEN scope, artifact permissions\n";
    output += "- Reliability: flaky test handling, retry logic, timeout configuration\n";
    output += "- Speed: parallel jobs, caching strategies, dependency optimization\n";
    output += "- Maintainability: reusable workflows, matrix builds, clear job naming\n";
    output += "- Best practices: pinned action versions, branch protection, status checks\n";
    output += "- Cost: unused jobs, excessive runner minutes, artifact retention\n";
    output += "\n";
    output += "Provide specific improvements as a git diff.\n";
    output += "\n";

    std::vector<std::string> ci_files;
    if (!ctx.git_root.empty()) {
        auto append = [&](std::vector<std::string> const& lines) {
            for (auto const& f : lines) ci_files.push_back(ctx.git_root.string() + "/" + f);
        };
        for (auto const& f : git_ls_files(ctx, {"*.yml", "*.yaml"})) {
            if (contains_ci_path(f)) ci_files.push_back(ctx.git_root.string() + "/" + f);
        }
        append(git_ls_files(ctx, {".gitlab-ci.yml", ".gitlab-ci.yaml", "**/.gitlab-ci.yml", "**/.gitlab-ci.yaml"}));
        append(git_ls_files(ctx, {"*Jenkinsfile*"}));
        append(git_ls_files(ctx, {"*Dockerfile*", "*dockerfile*", "*docker-compose*", "*compose.y*"}));
        append(git_ls_files(ctx, {".circleci/config.yml", "**/.circleci/config.yml"}));
        append(git_ls_files(ctx, {".travis.yml", "**/.travis.yml"}));
    }

    if (ci_files.empty()) {
        output += "No CI/CD configuration files found in this repository.\n";
        output += "Searched for: GitHub Actions, GitLab CI, Jenkins, Docker, CircleCI, Travis CI.\n";
        return {std::move(output), 0, false, std::string{}};
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
            output += "File: " + label.string() + "\n\n";
            output += "```" + infer_lang(p) + "\n";
            output += trim_context_nl(read_file(p), head_lines);
            output += "\n```\n\n";
        }
    }

    return {std::move(output), 0, false, std::string{}};
}

void render_help_ci(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt ci [--head N]
       echo "task description" | prompt ci [--head N]

CI/CD pipeline review prompt.
Automatically finds and embeds all CI/CD configuration files from the repository
(GitHub Actions, GitLab CI, Jenkins, Docker, etc.).

Options:
  --head N   Keep only the first N lines of each embedded context
)EOF";
}

} // namespace prompt::prompts
