#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/prompts/prompts.hpp"
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

std::string git_raw(std::vector<std::string> const& args) { return prompt::process::run_command(args).stdout_data; }

std::string strip_trailing_newlines(std::string_view s) {
    std::string out(s);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::string infer_lang(std::string_view base) {
    auto dot = base.rfind('.');
    std::string_view ext = (dot == std::string_view::npos) ? base : base.substr(dot + 1);
    if (base == "Dockerfile") return "dockerfile";
    if (base == "Makefile" || base == "makefile" || base == "GNUmakefile") return "makefile";
    if (base == "CMakeLists.txt") return "cmake";
    if (ext == "c" || ext == "h") return "c";
    if (ext == "cc" || ext == "cp" || ext == "cpp" || ext == "cxx" || ext == "c++" || ext == "hpp" || ext == "hxx" ||
        ext == "hh" || ext == "h++")
        return "cpp";
    if (ext == "rs") return "rust";
    if (ext == "py" || ext == "pyi") return "python";
    if (ext == "sh" || ext == "bash") return "bash";
    if (ext == "js" || ext == "cjs" || ext == "mjs") return "javascript";
    if (ext == "ts" || ext == "mts" || ext == "cts") return "typescript";
    if (ext == "jsx") return "jsx";
    if (ext == "tsx") return "tsx";
    if (ext == "java") return "java";
    if (ext == "go") return "go";
    if (ext == "rb") return "ruby";
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
    return "text";
}

} // namespace

prompt_output execute_git_diff(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    if (prompt::process::run_command({"git", "rev-parse", "--is-inside-work-tree"}).exit_code != 0) {
        throw soft_error{"prompt git.diff: Error: Not inside a git repository.\n"};
    }

    std::vector<std::string> files;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        files.emplace_back(ctx.args[i]);
    }

    std::filesystem::path git_root = ctx.git_root;

    std::string output;
    if (!files.empty()) {
        std::vector<std::string> args = {"git", "diff", "--"};
        for (auto const& f : files) args.push_back(f);
        std::string diff_output = strip_trailing_newlines(git_raw(args));
        if (!diff_output.empty()) {
            output += "Diffs:\n";
            output += "\n";
            output += "```diff\n";
            output += trim_context_nl(diff_output, head_lines);
            output += "```\n";
        } else {
            return {std::string{}, "No diffs found for the specified files.\n", false, false};
        }
    } else {
        std::string unstaged_diff = strip_trailing_newlines(git_raw({"git", "diff"}));
        std::string untracked = git_raw({"git", "ls-files", "--others", "--exclude-standard"});
        std::string untracked_stripped = strip_trailing_newlines(untracked);

        if (!unstaged_diff.empty()) {
            output += "Unstaged changes:\n";
            output += "\n";
            output += "```diff\n";
            output += trim_context_nl(unstaged_diff, head_lines);
            output += "```\n";
            output += "\n";
        }

        if (!untracked_stripped.empty()) {
            output += "Untracked files:\n";
            output += "\n";
            // `while IFS= read -r filepath; do ... done <<< "$untracked"` — the
            // <<< adds one trailing newline, so split the stripped list on \n.
            std::size_t start = 0;
            while (start <= untracked_stripped.size()) {
                std::size_t nl = untracked_stripped.find('\n', start);
                std::string filepath = (nl == std::string::npos) ? untracked_stripped.substr(start)
                                                                 : untracked_stripped.substr(start, nl - start);
                std::filesystem::path full = git_root / filepath;
                std::error_code ec;
                if (!filepath.empty() && std::filesystem::is_regular_file(full, ec) && !ec) {
                    std::string name = std::filesystem::path(filepath).filename().string();
                    std::string lang = infer_lang(name);
                    std::string content = read_file(full);
                    output += std::format("File: {} (new)\n", filepath);
                    output += "\n";
                    output += std::format("```{}\n", lang);
                    output += trim_context_nl(content, head_lines);
                    output += "```\n";
                    output += "\n";
                }
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
        }

        if (unstaged_diff.empty() && untracked_stripped.empty()) {
            return {std::string{}, "No changes found in the working tree.\n", false, false};
        }
    }

    return {std::move(output), std::string{}, false, false};
}

void render_help_git_diff(std::ostream& os) {
    os << R"EOF(Usage: prompt git.diff [--head N] [FILE...]

Shows Git diffs for the specified files.
If no files are provided, shows all unstaged changes and untracked files.

Options:
  --head N   Keep only the first N lines of embedded context
)EOF";
}

} // namespace prompt::prompts
