#include "prompt/prompts/git_worktree_files_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

namespace {

std::string git_raw(std::vector<std::string> const& args) noexcept {
    std::vector<char const*> argv;
    argv.reserve(args.size() + 1);
    for (auto const& s : args) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    return prompt::process::run_command(argv).stdout_data;
}

std::string strip_trailing_newlines(std::string_view s) noexcept {
    std::string out(s);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::string infer_lang(std::string_view base) noexcept {
    auto dot = base.rfind('.');
    std::string_view ext = (dot == std::string_view::npos) ? base : base.substr(dot + 1);
    if (base == "Dockerfile") return "dockerfile";
    if (base == "Makefile" || base == "makefile" || base == "GNUmakefile") return "makefile";
    if (base == "CMakeLists.txt") return "cmake";
    if (ext == "c" || ext == "h") return "c";
    if (ext == "cc" || ext == "cp" || ext == "cpp" || ext == "cxx" || ext == "c++" || ext == "hpp" || ext == "hxx" ||
        ext == "hh" || ext == "h++")
        return "cpp";
    if (ext == "mm") return "objective-cpp";
    if (ext == "m") return "objectivec";
    if (ext == "rs") return "rust";
    if (ext == "py" || ext == "pyi") return "python";
    if (ext == "sh" || ext == "bash") return "bash";
    if (ext == "js" || ext == "cjs" || ext == "mjs") return "javascript";
    if (ext == "ts" || ext == "mts" || ext == "cts") return "typescript";
    if (ext == "jsx") return "jsx";
    if (ext == "tsx") return "tsx";
    if (ext == "java") return "java";
    if (ext == "kt" || ext == "kts") return "kotlin";
    if (ext == "go") return "go";
    if (ext == "rb") return "ruby";
    if (ext == "lua") return "lua";
    if (ext == "json" || ext == "stock" || ext == "tse") return "json";
    if (ext == "jsonc") return "jsonc";
    if (ext == "yaml" || ext == "yml") return "yaml";
    if (ext == "toml") return "toml";
    if (ext == "md") return "markdown";
    if (ext == "txt" || ext == "log") return "text";
    if (ext == "diff" || ext == "patch") return "diff";
    if (ext == "html" || ext == "htm") return "html";
    if (ext == "css") return "css";
    if (ext == "xml") return "xml";
    if (ext == "svg") return "svg";
    if (ext == "ini" || ext == "cfg" || ext == "conf") return "ini";
    if (ext == "sql") return "sql";
    if (ext == "r") return "r";
    return "text";
}

// Port of `changed_files=($(git diff ... --name-only))` — bash word-splits the
// captured output on IFS (space/tab/newline).
std::vector<std::string> word_split(std::string_view s) noexcept {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        std::size_t start = i;
        while (i < s.size() && !(s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

std::string detect_base_branch(std::string const& given) noexcept {
    if (!given.empty()) return given;
    std::string upstream = strip_trailing_newlines(git_raw({"git", "rev-parse", "--abbrev-ref", "@{u}"}));
    if (upstream.empty()) return "origin/main";
    return upstream;
}

} // namespace

prompt_result execute_git_worktree_files(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::vector<char const*> rev = {"git", "rev-parse", "--is-inside-work-tree", nullptr};
    if (prompt::process::run_command(rev).exit_code != 0) {
        return {std::string{}, 1, false, "prompt git.worktree.files: Error: Not inside a git repository.\n"};
    }

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
            if (i + 1 >= rest.size()) return {std::string{}, 2, false, "Missing value for --not\n"};
            not_commit = rest[i + 1];
            i += 2;
        } else if (rest[i] == "--base") {
            if (i + 1 >= rest.size()) return {std::string{}, 2, false, "Missing value for --base\n"};
            base_branch = rest[i + 1];
            i += 2;
        } else {
            files.push_back(rest[i]);
            ++i;
        }
    }

    base_branch = detect_base_branch(base_branch);

    std::vector<std::string> changed_files;
    if (!files.empty()) {
        changed_files = files;
    } else {
        std::vector<std::string> diff_args = {base_branch};
        if (!not_commit.empty()) {
            diff_args.push_back("--not");
            diff_args.push_back(not_commit);
        }
        std::vector<std::string> git_cmd = {"git", "diff"};
        for (auto const& a : diff_args) git_cmd.push_back(a);
        git_cmd.push_back("--name-only");
        changed_files = word_split(git_raw(git_cmd));
    }

    if (changed_files.empty()) {
        return {std::string{}, 0, false, "No changed files found between current branch and " + base_branch + ".\n"};
    }

    std::optional<std::filesystem::path> git_root;
    if (!ctx.git_root.empty()) git_root = ctx.git_root;

    std::string output;
    std::string error;
    for (auto const& file : changed_files) {
        auto resolved = ctx.resolve_input_file(file);
        if (!resolved) {
            error += "prompt git.worktree.files: file not found: " + file + "\n";
            continue;
        }
        if (::access(resolved->c_str(), R_OK) != 0) {
            error += "prompt git.worktree.files: file not readable: \"" + file + "\"; resolved to \"" +
                     resolved->string() + "\"\n";
            continue;
        }

        auto rel = fs::relative_path(*resolved, git_root);
        auto lang = infer_lang(resolved->filename().string());
        std::string raw = read_file(*resolved);
        if (strip_trailing_newlines(raw).empty()) continue;

        output += "File " + rel.string();
        if (head_lines > 0) output += " (first " + std::to_string(head_lines) + " lines)";
        output += "\n\n";
        output += "```" + lang + "\n";
        output += trim_context_nl(raw, head_lines);
        output += "\n```\n\n";
    }

    if (!error.empty()) return {std::move(output), 0, false, std::move(error)};
    return {std::move(output), 0, false};
}

void render_help_git_worktree_files(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt git.worktree.files [--not <commit>] [--base <branch>] [--head N] [FILE...]

Embeds files changed since the upstream branch as Markdown code blocks.
By default, compares against the upstream tracking branch (or origin/main).
Explicit FILE arguments restrict embedding to only those changed files.

Options:
  --not <commit>   Exclude a commit (and its ancestors) from the diff
  --base <branch>  Base branch to compare against (default: upstream or origin/main)
  --head N         Keep only the first N lines of each embedded file
)EOF";
}

} // namespace prompt::prompts
