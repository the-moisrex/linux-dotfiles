#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace prompt {

std::string infer_lang(std::filesystem::path const& file) noexcept;

std::string trim_context(std::string_view content, std::size_t head_lines) noexcept;

// Port of `_common.sh trim_context`: the content with its trailing newlines
// dropped (bash always captures through $(...), which strips them) followed by
// exactly one newline, capped at head_lines lines (0 = no cap).
std::string trim_context_nl(std::string_view content, std::size_t head_lines) noexcept;

std::string read_file(std::filesystem::path const& path) noexcept;

std::string embed_file(std::filesystem::path const& path, std::string_view label = {},
                       std::size_t head_lines = 0) noexcept;

// Port of `_common.sh embed_stdin`: the stdin content with trailing newlines
// dropped, followed by a blank line. head_lines is ignored by bash (only
// trim_context applies it), so it is ignored here too.
std::string embed_stdin(std::string_view content, std::size_t head_lines) noexcept;

} // namespace prompt