#include "environment.hpp"
#include "core/clipboard.hpp"
#include "core/fs.hpp"
#include "core/process.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

namespace {

// `fzf -m` over whatever the caller pipes in: the file prompts' interactive
// picker, used when a prompt is invoked with no FILE arguments.
std::vector<std::filesystem::path> select_files_with_fzf() {
    auto result = process::run_command(std::vector<std::string>{"fzf", "-m"});
    std::vector<std::filesystem::path> files;
    std::string line;
    for (char c : result.stdout_data) {
        if (c == '\n') {
            if (!line.empty()) {
                files.emplace_back(line);
                line.clear();
            }
        } else {
            line += c;
        }
    }
    if (!line.empty()) files.emplace_back(line);
    return files;
}

// Resolve a loose path argument: an existing file, then the same path under the
// git root, then a fuzzy search of the tracked/untracked files. stderr is
// silenced because fzf complains without a tty and most callers capture the
// result through a command substitution.
std::optional<std::filesystem::path> resolve_input_file_with_fzf(std::string_view name) {
    std::filesystem::path p(name);
    if (std::filesystem::is_regular_file(p)) return p;

    auto git_root = fs::find_git_root();
    if (git_root) {
        auto full = *git_root / name;
        if (std::filesystem::is_regular_file(full)) return full;
    }

    std::string name_str(name);
    std::vector<std::string> storage = {"bash", "-c"};
    if (git_root) {
        storage.emplace_back("cd \"$0\" && git ls-files --cached --others --exclude-standard 2>/dev/null | "
                             "fzf -f \"$1\" 2>/dev/null | head -n 1");
        storage.push_back(git_root->string());
    } else {
        storage.emplace_back("rg --files 2>/dev/null || find . -type f | fzf -f \"$0\" 2>/dev/null | head -n 1");
    }
    storage.push_back(name_str);

    auto result = process::run_command(storage, "");
    if (!result.stdout_data.empty()) {
        std::string line = result.stdout_data.substr(0, result.stdout_data.find('\n'));
        std::filesystem::path selected(line);
        if (std::filesystem::is_regular_file(selected)) return selected;
    }

    return std::nullopt;
}

} // namespace

environment make_environment() {
    return environment{
        .git_root = fs::find_git_root().value_or(std::filesystem::path{}),
        .exe_path = fs::self_path().value_or(std::filesystem::path{}),
        .clipboard_content = &clipboard::paste,
        .clipboard_identifier = [] -> std::optional<std::string> {
            return clipboard::detect_identifier(clipboard::paste());
        },
        .select_files = &select_files_with_fzf,
        .resolve_input_file = &resolve_input_file_with_fzf,
    };
}

} // namespace prompt
