#include "fs.hpp"
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace prompt::fs {

std::optional<std::filesystem::path> find_git_root(std::filesystem::path const& start) {

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
                                    std::optional<std::filesystem::path> const& git_root) {

    try {
        if (git_root && std::filesystem::exists(*git_root)) {
            return std::filesystem::relative(file, *git_root);
        }
        return std::filesystem::relative(file, std::filesystem::current_path());
    } catch (...) {
        return file.filename();
    }
}

std::optional<std::filesystem::path> self_path() {
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return std::nullopt;
    return exe;
}

std::filesystem::path bin_tool(std::string_view name, std::filesystem::path const& exe_path,
                               std::optional<std::filesystem::path> const& git_root) {
    std::error_code ec;
    std::filesystem::path bare(name);

    auto is_tool = [&](std::filesystem::path const& p) {
        return std::filesystem::is_regular_file(p, ec) &&
               ((std::filesystem::status(p, ec).permissions() & std::filesystem::perms::owner_exec) !=
                std::filesystem::perms::none);
    };

    std::vector<std::filesystem::path> candidates;
    if (!exe_path.empty()) {
        auto exe_dir = exe_path.parent_path();
        candidates.push_back(exe_dir / bare); // installed layout: prompt sits in bin/
        candidates.push_back(exe_dir.parent_path().parent_path() / "bin" /
                             bare); // build layout: .../prompts/build/prompt
    }
    if (git_root) candidates.push_back(*git_root / "bin" / bare);
    // Walk up from cwd (covers running from a repo subdirectory when the
    // executable lives elsewhere, e.g. installed).
    auto dir = std::filesystem::current_path(ec);
    for (std::size_t i = 0; i < 8 && !dir.empty(); ++i) {
        candidates.push_back(dir / "bin" / bare);
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }

    for (auto const& cand : candidates) {
        if (is_tool(cand)) return cand;
    }
    // Fall back to PATH, resolved to an absolute path: callers may invoke the
    // tool through an interpreter (`python3 <tool>`), where a bare name would
    // be opened relative to the cwd instead of searched on PATH.
    if (char const* path_env = std::getenv("PATH")) {
        std::string const path_str(path_env);
        std::size_t start = 0;
        while (start <= path_str.size()) {
            auto const end = path_str.find(':', start);
            std::string const entry =
                path_str.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!entry.empty()) {
                std::filesystem::path const cand = std::filesystem::path(entry) / bare;
                if (is_tool(cand)) return std::filesystem::absolute(cand, ec);
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    // Last resort: let the shell resolve it via PATH.
    return bare;
}

} // namespace prompt::fs
