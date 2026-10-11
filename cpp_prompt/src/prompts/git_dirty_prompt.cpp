#include "prompt/prompts/git_dirty_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
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

// Port of `var="$(git ...)"` — trailing newlines stripped (matters for the
// `[[ -n ]]` content checks and the `echo "$var"` renders).
std::string capture(std::vector<std::string> const& args) noexcept {
    std::string out = git_raw(args);
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
    if (ext == "zsh") return "zsh";
    if (ext == "fish") return "fish";
    if (ext == "nu") return "nu";
    if (ext == "js" || ext == "cjs" || ext == "mjs") return "javascript";
    if (ext == "ts" || ext == "mts" || ext == "cts") return "typescript";
    if (ext == "jsx") return "jsx";
    if (ext == "tsx") return "tsx";
    if (ext == "java") return "java";
    if (ext == "kt" || ext == "kts") return "kotlin";
    if (ext == "swift") return "swift";
    if (ext == "go") return "go";
    if (ext == "rb") return "ruby";
    if (ext == "php") return "php";
    if (ext == "lua") return "lua";
    if (ext == "pl" || ext == "pm") return "perl";
    if (ext == "r") return "r";
    if (ext == "scala") return "scala";
    if (ext == "cs") return "csharp";
    if (ext == "fs" || ext == "fsx") return "fsharp";
    if (ext == "vb") return "vbnet";
    if (ext == "dart") return "dart";
    if (ext == "ex" || ext == "exs") return "elixir";
    if (ext == "erl" || ext == "hrl") return "erlang";
    if (ext == "clj" || ext == "cljs" || ext == "cljc") return "clojure";
    if (ext == "ml" || ext == "mli") return "ocaml";
    if (ext == "sql") return "sql";
    if (ext == "html" || ext == "htm") return "html";
    if (ext == "css") return "css";
    if (ext == "scss") return "scss";
    if (ext == "sass") return "sass";
    if (ext == "less") return "less";
    if (ext == "xml") return "xml";
    if (ext == "xsl" || ext == "xslt") return "xslt";
    if (ext == "svg") return "svg";
    if (ext == "json" || ext == "stock" || ext == "tse") return "json";
    if (ext == "jsonc") return "jsonc";
    if (ext == "yaml" || ext == "yml") return "yaml";
    if (ext == "toml") return "toml";
    if (ext == "ini" || ext == "cfg" || ext == "conf") return "ini";
    if (ext == "env") return "dotenv";
    if (ext == "md") return "markdown";
    if (ext == "txt" || ext == "log") return "text";
    if (ext == "diff" || ext == "patch") return "diff";
    if (ext == "proto") return "proto";
    if (ext == "asm" || ext == "s" || ext == "S") return "asm";
    if (ext == "tex") return "tex";
    if (ext == "vim") return "vim";
    return "text";
}

// Split an already-stripped (\n-free-trailing) string on '\n'; no trailing
// empty entry — mirrors the `<<< "$var"` here-strings the script feeds to read.
std::vector<std::string> split_lines(std::string_view s) noexcept {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::size_t start = 0;
    while (true) {
        std::size_t nl = s.find('\n', start);
        if (nl == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, nl - start));
        start = nl + 1;
        if (start == s.size()) break;
    }
    return out;
}

} // namespace

prompt_result execute_git_dirty(prompt_context&& ctx) noexcept {
    // git-dirty.sh parses its own flags (not via _common.sh's parse_arguments),
    // and its --head accepts any value verbatim. --help/-h are intercepted by
    // the native dispatcher before we run.
    std::size_t head_lines = 0;
    bool filter_staged = false;
    bool filter_unstaged = false;
    bool filter_all = false;
    bool except = false;
    bool output_files = false;
    bool output_diff = false;
    bool output_full = false;
    bool output_full_diff = false;

    for (std::size_t i = 0; i < ctx.args_count;) {
        std::string_view a = ctx.args[i];
        if (a == "--help" || a == "-h") {
            ++i; // handled by the dispatcher; unreachable in practice
        } else if (a == "--head") {
            if (i + 1 >= ctx.args_count) {
                return {std::string{}, 2, false, "Missing value for --head\n"};
            }
            head_lines = static_cast<std::size_t>(std::strtoull(std::string(ctx.args[i + 1]).c_str(), nullptr, 10));
            i += 2;
        } else if (a == "--staged" || a == "-s") {
            filter_staged = true;
            ++i;
        } else if (a == "--uncommitted" || a == "-u") {
            filter_unstaged = true;
            ++i;
        } else if (a == "--all" || a == "-a") {
            filter_all = true;
            ++i;
        } else if (a == "--except") {
            except = true;
            ++i;
        } else if (a == "--files" || a == "-l") {
            output_files = true;
            ++i;
        } else if (a == "--diff" || a == "-d") {
            output_diff = true;
            ++i;
        } else if (a == "--full" || a == "-f") {
            output_full = true;
            ++i;
        } else if (a == "--full-diff" || a == "-F") {
            output_full_diff = true;
            ++i;
        } else {
            return {std::string{}, 1, false,
                    "prompt git-dirty: unknown option: " + std::string(a) +
                        "\nTry 'prompt git-dirty --help' for usage.\n"};
        }
    }

    if (!filter_staged && !filter_unstaged && !filter_all) filter_all = true;
    if (!output_files && !output_diff && !output_full && !output_full_diff) output_files = true;

    std::vector<char const*> rev = {"git", "rev-parse", "--is-inside-work-tree", nullptr};
    if (prompt::process::run_command(rev).exit_code != 0) {
        return {std::string{}, 1, false, "prompt git-dirty: Error: Not inside a git repository.\n"};
    }

    std::filesystem::path git_root = ctx.git_root;

    std::string staged_files;
    std::string staged_diff;
    std::string unstaged_files;
    std::string unstaged_diff;
    std::string untracked_files;

    if (filter_staged) {
        staged_files = capture({"git", "diff", "--cached", "--name-status"});
        staged_diff = capture({"git", "diff", "--cached"});
    }
    if (filter_unstaged) {
        unstaged_files = capture({"git", "diff", "--name-status"});
        unstaged_diff = capture({"git", "diff"});
        untracked_files = capture({"git", "ls-files", "--others", "--exclude-standard"});
    }
    if (filter_all) {
        staged_files = capture({"git", "diff", "--cached", "--name-status"});
        staged_diff = capture({"git", "diff", "--cached"});
        unstaged_files = capture({"git", "diff", "--name-status"});
        unstaged_diff = capture({"git", "diff"});
        untracked_files = capture({"git", "ls-files", "--others", "--exclude-standard"});
    }

    // --except inverts the active filter. The branch order (staged, unstaged,
    // all) matches the script's if/elif/elif, so e.g. --staged --all --except
    // takes the staged branch.
    if (except) {
        if (filter_staged) {
            unstaged_files = capture({"git", "diff", "--name-status"});
            unstaged_diff = capture({"git", "diff"});
            untracked_files = capture({"git", "ls-files", "--others", "--exclude-standard"});
            staged_files.clear();
            staged_diff.clear();
        } else if (filter_unstaged) {
            staged_files = capture({"git", "diff", "--cached", "--name-status"});
            staged_diff = capture({"git", "diff", "--cached"});
            unstaged_files.clear();
            unstaged_diff.clear();
            untracked_files.clear();
        } else if (filter_all) {
            staged_files.clear();
            staged_diff.clear();
            unstaged_files.clear();
            unstaged_diff.clear();
            untracked_files.clear();
        }
    }

    bool has_content = !staged_files.empty() || !unstaged_files.empty() || !untracked_files.empty();
    if (!has_content) {
        return {std::string{}, 0, false, "No changes found in the repository.\n"};
    }

    std::string output;
    output += "\n"; // the unconditional `echo` after the content check

    // Reads GIT_ROOT/filepath as a full file block with the given heading
    // suffix (e.g. "(new)" or "(status: M)"); a no-op when the file is absent.
    auto emit_file = [&](std::string const& filepath, std::string const& heading) {
        std::filesystem::path full = git_root / filepath;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(full, ec) || ec) return;
        std::string name = full.filename().string();
        std::string content = read_file(full);
        output += "File: " + filepath + " " + heading + "\n";
        output += "\n";
        output += "```" + infer_lang(name) + "\n";
        output += trim_context_nl(content, head_lines);
        output += "```\n";
        output += "\n";
    };

    // Iterate a name-status list ("M\tpath" per line), splitting on the first
    // tab exactly like `IFS=$'\t' read -r status filepath`.
    auto each_name_status = [&](std::string const& files, auto&& fn) {
        for (auto const& line : split_lines(files)) {
            std::size_t tab = line.find('\t');
            std::string status = line.substr(0, tab);
            std::string filepath = (tab == std::string::npos) ? std::string{} : line.substr(tab + 1);
            fn(status, filepath);
        }
    };

    if (output_files) {
        if (!staged_files.empty()) output += "Staged changes:\n```text\n" + staged_files + "\n```\n\n";
        if (!unstaged_files.empty()) output += "Unstaged changes:\n```text\n" + unstaged_files + "\n```\n\n";
        if (!untracked_files.empty()) output += "Untracked files:\n```text\n" + untracked_files + "\n```\n\n";
    } else if (output_diff) {
        if (!staged_diff.empty()) {
            output += "Staged diff:\n\n```diff\n" + trim_context_nl(staged_diff, head_lines) + "```\n\n";
        }
        if (!unstaged_diff.empty()) {
            output += "Unstaged diff:\n\n```diff\n" + trim_context_nl(unstaged_diff, head_lines) + "```\n\n";
        }
        if (!untracked_files.empty()) {
            output += "Untracked files:\n\n";
            for (auto const& filepath : split_lines(untracked_files)) emit_file(filepath, "(new)");
        }
    } else if (output_full) {
        if (!staged_files.empty()) {
            output += "Staged files:\n\n";
            each_name_status(staged_files, [&](std::string const& st, std::string const& fp) {
                emit_file(fp, "(status: " + st + ")");
            });
        }
        if (!unstaged_files.empty()) {
            output += "Unstaged files:\n\n";
            each_name_status(unstaged_files, [&](std::string const& st, std::string const& fp) {
                emit_file(fp, "(status: " + st + ")");
            });
        }
        if (!untracked_files.empty()) {
            output += "Untracked files:\n\n";
            for (auto const& filepath : split_lines(untracked_files)) emit_file(filepath, "(new)");
        }
    } else if (output_full_diff) {
        if (!staged_files.empty()) {
            output += "Staged files:\n\n";
            each_name_status(staged_files, [&](std::string const& st, std::string const& fp) {
                emit_file(fp, "(status: " + st + ")");
            });
            if (!staged_diff.empty()) {
                output += "Diffs:\n\n```diff\n" + trim_context_nl(staged_diff, head_lines) + "```\n\n";
            }
        }
        if (!unstaged_files.empty()) {
            output += "Unstaged files:\n\n";
            each_name_status(unstaged_files, [&](std::string const& st, std::string const& fp) {
                emit_file(fp, "(status: " + st + ")");
            });
            if (!unstaged_diff.empty()) {
                output += "Diffs:\n\n```diff\n" + trim_context_nl(unstaged_diff, head_lines) + "```\n\n";
            }
        }
        if (!untracked_files.empty()) {
            output += "Untracked files:\n\n";
            for (auto const& filepath : split_lines(untracked_files)) emit_file(filepath, "(new)");
        }
    }

    return {std::move(output), 0, false};
}

void render_help_git_dirty(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt git-dirty [OPTIONS]

Show uncommitted/changed files in the current Git repository.
By default it lists all staged and unstaged changes (--all --files);
the options below switch to diffs or full file contents.

FILTER OPTIONS (what to show):
  --staged, -s           Only staged (cached) changes
  --uncommitted, -u      Only unstaged working tree changes
  --all, -a              Both staged and unstaged (default)
  --except               Invert the active filter
                         e.g. --staged --except = everything except staged
                              --all --except    = nothing (no filter)

OUTPUT OPTIONS (how to show):
  --files, -l            List filenames with status (default)
  --diff, -d             Show only the diffs
  --full, -f             Show full file contents
  --full-diff, -F        Show full files + diff context (@@ hunks with +/-)

OTHER:
  --head N               Keep only the first N lines of embedded context
  -h, --help             Show this help

DEFAULTS: --all --files

EXAMPLES:
  prompt git-dirty                          # All changes, file list
  prompt git-dirty --staged --diff          # Staged diffs only
  prompt git-dirty --uncommitted --full     # Full contents of unstaged files
  prompt git-dirty --staged --except        # Everything except staged
  prompt git-dirty --full-diff              # Full files with diff markers
)EOF";
}

} // namespace prompt::prompts
