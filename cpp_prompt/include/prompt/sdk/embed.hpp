#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <fstream>

namespace prompt {

std::string infer_lang(std::filesystem::path const& file) noexcept;

std::string trim_context(std::string_view content, std::size_t head_lines) noexcept;

std::string read_file(std::filesystem::path const& path) noexcept;

std::string embed_file(
    std::filesystem::path const& path,
    std::string_view label = {},
    std::size_t head_lines = 0) noexcept;

std::string embed_stdin(std::string_view content, std::size_t head_lines) noexcept;

} // namespace prompt