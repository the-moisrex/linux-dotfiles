#include "../core/process.hpp"
#include "../sdk/prompt_registry.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::prompts {

namespace {

// Single-quote a value for embedding inside a bash -c pipeline the prompt will
// re-parse (run_command adds the outer layer of quoting).
std::string sh_quote(std::string const& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else
            out += c;
    }
    out += '\'';
    return out;
}

// Port of `git ls-files [-- DIR] | tree [-L depth] --fromfile`. Run as one
// bash -c pipeline: tree reads the file list from the pipe exactly like the
// script (its colour/summary output depends on how it is invoked — run_pipeline
// feeds it a temp file and tree changes its default output under popen).
std::string run_tree(std::vector<std::string> const& ls_dirs, std::span<const std::string> tree_args) {
    std::string ls = "git ls-files";
    if (!ls_dirs.empty()) {
        ls += " --";
        for (auto const& d : ls_dirs) ls += ' ' + sh_quote(d);
    }
    std::string tr = "tree";
    for (auto const& a : tree_args) tr += ' ' + sh_quote(a);
    tr += " --fromfile";
    std::vector<std::string> storage = {"bash", "-c", ls + " | " + tr};
    return prompt::process::run_command(storage).stdout_data;
}

} // namespace

static void execute_repo(prompt_context& ctx) {
    // repo.sh scans every arg for --help/-h itself (before the git check);
    // the native dispatcher already intercepted those before we run.

    if (prompt::process::run_command(std::vector<std::string>{"git", "rev-parse", "--is-inside-work-tree"}).exit_code !=
        0) {
        throw soft_error{"prompt repo: Not inside a git repository.\n"};
    }

    std::string depth;
    std::vector<std::string> dirs;
    for (std::size_t i = 0; i < ctx.args_count;) {
        std::string_view a = ctx.args[i];
        if (a == "--depth") {
            if (i + 1 >= ctx.args_count) throw usage_error{"Missing value for --depth\n"};
            depth = std::string(ctx.args[i + 1]);
            i += 2;
        } else {
            dirs.emplace_back(a);
            ++i;
        }
    }

    std::string output;
    output += "# Repository file structure (git-tracked files)\n\n";

    std::vector<std::string> tree_args;
    if (!depth.empty()) {
        tree_args.emplace_back("-L");
        tree_args.push_back(depth);
    }

    if (dirs.empty()) {
        output += run_tree({}, tree_args);
    } else {
        for (auto const& dir : dirs) output += run_tree({dir}, tree_args);
    }

    ctx.out = std::move(output);
}

static void render_help_repo(std::ostream& os) {
    os << R"EOF(Usage: prompt repo [--depth N] [DIR...]

Displays the repository's file structure using only git-tracked files.
Useful for giving an AI a high-level view of the codebase.

Options:
  --depth N   Limit tree depth (default: unlimited)
  --help, -h  Show this help message

Arguments:
  DIR...      Optional directory prefixes to limit the view (e.g., bin/ prompts/)
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "repo",
    "Displays the repository's file structure using only git-tracked files. Useful for giving an AI "
    "a high-level view of the codebase.",
    &execute_repo, &render_help_repo);

} // namespace prompt::prompts
