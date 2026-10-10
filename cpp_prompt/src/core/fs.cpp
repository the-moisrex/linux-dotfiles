#include "prompt/core/fs.hpp"
#include <cstdlib>
#include <string>

namespace prompt::fs {

std::vector<std::filesystem::path> xdg_config_dirs() noexcept {
    std::vector<std::filesystem::path> dirs;
    char const* xdg = std::getenv("XDG_CONFIG_DIRS");
    std::string_view dirs_str = xdg ? xdg : "/etc/xdg";

    std::size_t start = 0;
    while (start < dirs_str.size()) {
        auto end = dirs_str.find(':', start);
        if (end == std::string_view::npos) end = dirs_str.size();
        if (end > start) {
            dirs.emplace_back(dirs_str.substr(start, end - start));
        }
        start = end + 1;
    }
    return dirs;
}

std::vector<std::filesystem::path> prompt_search_dirs(std::filesystem::path const& exe_path) noexcept {

    std::vector<std::filesystem::path> dirs;

    for (auto const& base : xdg_config_dirs()) {
        dirs.push_back(base / "prompts");
    }

    auto repo_prompts = exe_path.parent_path().parent_path() / "prompts";
    if (std::filesystem::exists(repo_prompts)) {
        dirs.push_back(std::filesystem::canonical(repo_prompts));
    }

    return dirs;
}

std::optional<std::filesystem::path> find_prompt_file(std::string_view name,
                                                      std::span<std::filesystem::path const> search_dirs) noexcept {

    constexpr std::array<std::string_view, 4> exts = {".sh", ".txt", ".md", ""};

    for (auto const& dir : search_dirs) {
        if (!std::filesystem::exists(dir)) continue;
        for (auto const& ext : exts) {
            std::string filename = std::string(name) + std::string(ext);
            auto candidate = dir / filename;
            if (std::filesystem::is_regular_file(candidate)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

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

file_type detect_file_type(std::filesystem::path const& path) noexcept {
    auto ext = path.extension().string();
    if (ext == ".sh") return file_type::bash;
    if (ext == ".txt") return file_type::text;
    if (ext == ".md") return file_type::markdown;
    return file_type::unknown;
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

} // namespace prompt::fs