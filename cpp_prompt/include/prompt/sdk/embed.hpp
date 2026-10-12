#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace prompt {

std::string infer_lang(std::filesystem::path const& file);

std::string trim_context(std::string_view content, std::size_t head_lines);

// Port of `_common.sh trim_context`: the content with its trailing newlines
// dropped (bash always captures through $(...), which strips them) followed by
// exactly one newline, capped at head_lines lines (0 = no cap).
std::string trim_context_nl(std::string_view content, std::size_t head_lines);

std::string read_file(std::filesystem::path const& path);

std::string embed_file(std::filesystem::path const& path, std::string_view label = {}, std::size_t head_lines = 0);

// The file-embedding shape every file-prompt uses (28 sites): a "File: <name>"
// header, the content in a language-tagged fence, capped by --head, and a
// trailing blank line. Unlike embed_file this keeps the exact byte layout of
// the open-coded `output += ...` sequence (including the "File:" header and the
// blank line after the closing fence), so prompts can be simplified without
// changing a single output byte.
std::string embed_file_block(std::filesystem::path const& file, std::size_t head_lines);

// The warning every file-prompt prints to stderr when a FILE argument does not
// resolve to a readable regular file (10 identical sites), in the exact bytes
// the bash scripts emit (`error += ...` after a `[ -f ... ]` check fails).
void warn_missing_file(std::string& errors, std::string_view name);

// Port of `_common.sh embed_stdin`: the stdin content with trailing newlines
// dropped, followed by a blank line. head_lines is ignored by bash (only
// trim_context applies it), so it is ignored here too.
std::string embed_stdin(std::string_view content, std::size_t head_lines);

} // namespace prompt