#include "prompt/prompts/clang_tidy_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

std::string strip_trailing_newlines(std::string s) noexcept {
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// Realpath, invoked as the external tool so the printed path matches bash's
// `realpath "$check_path"` byte-for-byte.
std::string realpath_of(std::string const& p) noexcept {
    std::vector<char const*> argv = {"realpath", p.c_str(), nullptr};
    return strip_trailing_newlines(prompt::process::run_command(argv).stdout_data);
}

std::string git_toplevel() noexcept {
    std::vector<char const*> argv = {"git", "rev-parse", "--show-toplevel", nullptr};
    auto r = prompt::process::run_command(argv);
    return r.exit_code == 0 ? strip_trailing_newlines(r.stdout_data) : std::string{};
}

} // namespace

prompt_result execute_clang_tidy(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::vector<std::string> file_args;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        file_args.emplace_back(ctx.args[i]);
    }

    std::string git_root = git_toplevel();

    // clang-tidy.sh runs on "${ARGS[@]}" (parse_arguments strips --head) but
    // displays "$*" — the MAIN positional params, which parse_arguments never
    // touches (a bash function's `set --`/`shift` is local). So `$*` keeps
    // --head verbatim. The `if [ $# -eq 0 ]` guard tests the raw arg count.
    std::vector<std::string> run_args;     // ARGS
    std::vector<std::string> display_args; // $*
    if (ctx.args_count == 0) {
        auto selected = ctx.select_files();
        if (selected.empty()) {
            return {std::string{}, 0, false, "No files specified or selected.\n"};
        }
        for (auto const& s : selected) display_args.emplace_back(s.string());
        // Files from fzf only populate $@ (via `set --`), never ARGS.
    } else {
        for (std::size_t i = 0; i < ctx.args_count; ++i) display_args.emplace_back(ctx.args[i]);
        run_args = file_args;
    }

    // find_build_dir: scan $PWD then the git root for a compile_commands.json.
    std::string build_dir;
    {
        std::vector<std::string> bases;
        bases.emplace_back(".");
        if (!git_root.empty()) bases.push_back(git_root);
        static char const* common_dirs[] = {"build",
                                            "build-dev",
                                            "build-dev-clang",
                                            "build-dev-gcc",
                                            "cmake-build-debug",
                                            "cmake-build-release",
                                            "out",
                                            "build/Debug",
                                            "build/Release",
                                            "."};
        for (auto const& base : bases) {
            std::string found;
            for (char const* dir : common_dirs) {
                std::filesystem::path check = std::filesystem::path(base) / dir / "compile_commands.json";
                std::error_code ec;
                if (std::filesystem::is_regular_file(check, ec)) {
                    found = realpath_of((std::filesystem::path(base) / dir).string());
                    break;
                }
            }
            if (!found.empty()) {
                build_dir = found;
                break;
            }
        }
    }

    std::vector<std::string> ct_args;
    ct_args.emplace_back("--quiet");
    if (!build_dir.empty()) {
        ct_args.emplace_back("-p");
        ct_args.push_back(build_dir);
    }

    std::string command_str = "clang-tidy ";
    for (std::size_t i = 0; i < ct_args.size(); ++i) {
        if (i) command_str += ' ';
        command_str += ct_args[i];
    }
    for (auto const& a : display_args) {
        command_str += ' ';
        command_str += a;
    }

    // clang-tidy "${CT_ARGS[@]}" "${ARGS[@]}" 2>&1 || true
    std::vector<std::string> storage;
    storage.push_back("bash");
    storage.push_back("-c");
    storage.push_back("clang-tidy \"$@\" 2>&1");
    storage.push_back("clang-tidy");
    for (auto const& a : ct_args) storage.push_back(a);
    for (auto const& a : run_args) storage.push_back(a);

    std::vector<char const*> argv;
    argv.reserve(storage.size() + 1);
    for (auto const& s : storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    auto result = prompt::process::run_command(argv);

    std::string output;
    output += "\n";
    output += "Review the following `" + command_str +
              "` output. Identify the issues, explain why they were flagged, and provide the most robust "
              "and idiomatic fixes. Suggest small git patches or refactored code blocks where "
              "appropriate.\n";
    output += "\n";
    output += "\n```text\n";
    output += trim_context_nl(result.stdout_data, head_lines);
    output += "\n```\n";

    return {std::move(output), 0, false, std::string{}};
}

void render_help_clang_tidy(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt clang-tidy [--head N] [FILE...]
       some-command | prompt clang-tidy [--head N] [FILE...]

Runs `clang-tidy` on the provided files and outputs a prompt to fix the identified issues.
If no files are provided, `fzf -m` is used to choose them interactively.

Options:
  --head N   Keep only the first N lines of the output
  -h, --help Show this help
)EOF";
}

} // namespace prompt::prompts
