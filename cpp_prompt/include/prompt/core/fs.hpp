#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace prompt::fs {

std::vector<std::filesystem::path> xdg_config_dirs() noexcept;

std::vector<std::filesystem::path> prompt_search_dirs(std::filesystem::path const& exe_path) noexcept;

std::optional<std::filesystem::path> find_prompt_file(std::string_view name,
                                                      std::span<std::filesystem::path const> search_dirs) noexcept;

std::optional<std::filesystem::path>
find_git_root(std::filesystem::path const& start = std::filesystem::current_path()) noexcept;

enum class file_type { bash, text, markdown, unknown };

file_type detect_file_type(std::filesystem::path const& path) noexcept;

std::filesystem::path relative_path(std::filesystem::path const& file,
                                    std::optional<std::filesystem::path> const& git_root) noexcept;

} // namespace prompt::fs