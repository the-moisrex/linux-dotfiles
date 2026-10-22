#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace prompt {

// How to render an embedded file: `head_lines` caps the kept lines (--head) and
// `numbered` prefixes each kept line with its line number (--num/--numbered).
struct file_options {
    std::optional<std::size_t> head_lines;
    bool numbered = false;
};

std::string_view infer_lang(std::filesystem::path const& file);

// The content truncated to the first N lines (each keeping its newline) when a
// --head cap is present, or unchanged when it is absent. A cap of 0 keeps a
// single line — `sed` treats a range whose end precedes its start as just the
// first line.
std::string trim_context(std::string_view content, std::optional<std::size_t> head_lines);

// Like trim_context but for content that still carries its trailing newline
// (read straight from a file): the trailing newlines are dropped first and
// exactly one is added back. A --head cap of 0 keeps a single line, matching
// sed's `1,0p`.
std::string trim_context_nl(std::string_view content, std::optional<std::size_t> head_lines);

// Port of `printf '%s' "$text" | head -n N` captured through `$(...)`: the
// first N lines of text (N = 0 prints none), returned with no trailing
// newline. This is the shape run/clang-tidy use for their --head truncation
// (unlike trim_context, `head -n 0` really prints nothing).
std::string head_output(std::string_view text, std::size_t n);

std::string read_file(std::filesystem::path const& path);

// Prefix every line of `text` with a right-aligned line number and " | " when
// `numbered`; returns `text` unchanged otherwise. The numbers are relative to
// `text` (so a --head-trimmed file numbers 1..N) and the input's trailing
// newline, if any, is preserved exactly once.
std::string number_lines(std::string_view text, bool numbered);

std::string embed_file(std::filesystem::path const& path, std::string_view label = {}, file_options opts = {});

// The file-embedding shape every file-prompt uses: a "File: <name>" header, the
// content in a language-tagged fence, capped by --head, and a trailing blank
// line. Unlike embed_file this keeps the exact byte layout of the open-coded
// `output += ...` sequence (including the "File:" header and the blank line
// after the closing fence), so prompts can be simplified without changing the
// output layout.
std::string embed_file_block(std::filesystem::path const& file, file_options opts);

// The warning every file prompt prints to stderr when a FILE argument does not
// resolve to a readable regular file (10 identical sites).
void warn_missing_file(std::string& errors, std::string_view name);

// The stdin content with trailing newlines dropped, followed by a blank line.
// head_lines is ignored (only trim_context applies it), so it is ignored here.
std::string embed_stdin(std::string_view content, std::optional<std::size_t> head_lines);

} // namespace prompt
