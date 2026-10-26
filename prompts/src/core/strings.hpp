#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::text {

// The bash `$(...)` rule: trailing newlines are always stripped by command
// substitution. 15 prompt files carried a private copy of this.
std::string rstrip_newlines(std::string_view s);

// Trim leading and trailing newlines — a block boundary. Spaces and tabs are
// left alone (leading indentation in a code block is content, not padding).
std::string_view strip_newlines(std::string_view s);

// The one text-fold rule shared by the chain context, the stdout accumulator,
// the clipboard builder and the history replay: trim both sides, skip an empty
// block, separate consecutive blocks with exactly one blank line and terminate
// the buffer with exactly one newline. Returns whether it added anything.
bool append_block(std::string& text, std::string_view block);

// Strip trailing occurrences of `c` (default space).
std::string rstrip(std::string_view s, char c = ' ');

// Leading and trailing whitespace.
std::string strip(std::string_view s);

// Split on '\n'. With keep_blank=false (awk-like: skips empties) trailing
// newline does not produce a final empty element.
std::vector<std::string> split_lines(std::string_view s, bool keep_blank = false);

// Whitespace-separated words.
std::vector<std::string> words(std::string_view s);

// First N complete lines, no trailing newline (0 = all). Unlike
// sdk/embed.hpp's trim_context, this never appends a newline — it is for raw
// subprocess output.
std::string first_n_lines(std::string_view s, std::size_t n);

// Non-throwing integer parsing. When `s` is not a valid non-negative integer
// the bash code path it ports produced `fallback` (e.g. bash arithmetic
// treats a bare word as an unset variable → 0).
std::size_t to_size(std::string_view s, std::size_t fallback = 0);
long long to_ll(std::string_view s, long long fallback = 0);

// Escape every regex metacharacter (for interpolating $HOME/$USER into patterns).
std::string regex_escape(std::string_view s);

} // namespace prompt::text
