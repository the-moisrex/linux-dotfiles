#include "prompt/core/fs.hpp"
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace prompt::fs {

std::optional<std::filesystem::path> find_git_root(std::filesystem::path const& start) noexcept {

    auto current = start;
    while (true) {
        auto git_dir = current / ".git";
        if (std::filesystem::exists(git_dir)) {
            return current;
        }
        auto parent = current.parent_path();
        if (parent == current || parent.empty()) break;
        current = parent;
    }
    return std::nullopt;
}

std::filesystem::path relative_path(std::filesystem::path const& file,
                                    std::optional<std::filesystem::path> const& git_root) noexcept {

    try {
        if (git_root && std::filesystem::exists(*git_root)) {
            return std::filesystem::relative(file, *git_root);
        }
        return std::filesystem::relative(file, std::filesystem::current_path());
    } catch (...) {
        return file.filename();
    }
}

std::optional<std::filesystem::path> self_path() noexcept {
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return std::nullopt;
    return exe;
}

std::filesystem::path bin_tool(std::string_view name, std::filesystem::path const& exe_path,
                               std::optional<std::filesystem::path> const& git_root) noexcept {
    std::error_code ec;
    std::filesystem::path bare(name);

    std::vector<std::filesystem::path> candidates;
    if (!exe_path.empty()) {
        auto exe_dir = exe_path.parent_path();
        candidates.push_back(exe_dir / "bin" / bare);                             // installed layout
        candidates.push_back(exe_dir.parent_path().parent_path() / "bin" / bare); // build layout
    }
    if (git_root) candidates.push_back(*git_root / "bin" / bare);
    // Walk up from cwd (covers running from a repo subdirectory when the
    // executable lives elsewhere, e.g. installed).
    auto dir = std::filesystem::current_path();
    for (std::size_t i = 0; i < 8 && !dir.empty(); ++i) {
        candidates.push_back(dir / "bin" / bare);
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }

    for (auto const& cand : candidates) {
        if (std::filesystem::is_regular_file(cand, ec)) {
            if ((std::filesystem::status(cand, ec).permissions() & std::filesystem::perms::owner_exec) !=
                std::filesystem::perms::none) {
                return cand;
            }
        }
    }
    // Last resort: let the shell resolve it via PATH.
    return bare;
}

} // namespace prompt::fs
