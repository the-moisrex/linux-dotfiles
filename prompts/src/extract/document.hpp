#pragma once

// The Markdown grammar behind every extractor: blocks parsed together with
// their context (the enclosing heading path plus the prose paragraph before and
// after), so "the csv block whose intro says X" is two independent filters.
// Ported from the former `bin/md.extract` Python engine; the extractors and the
// standalone CLI both read this one representation.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::extract {

struct block {
    std::string kind; // "code" | "table" | "section" | "frontmatter"
    std::string file;
    std::size_t start_line = 1;
    std::size_t end_line = 1;
    std::string body;
    std::size_t index = 0;
    std::string lang_raw;
    std::string lang;
    std::string info_raw;
    std::string fence;
    std::string title;
    std::string ident;
    std::vector<std::pair<int, std::string>> heading_path;
    int heading_level = 0;
    std::string before;
    std::string after;
    bool closed = true;
    int file_order = 0;
    std::string program;
};

struct parse_options {
    bool indented = false;
    bool include_subsections = true;
};

[[nodiscard]] std::vector<block> parse_document(std::string_view text, std::string_view name, int file_order = 0,
                                                parse_options options = {});
void assign_indices(std::vector<block>& blocks);

// The language family of a fence info string (`c++`/`cxx` → `cpp`, `bash` →
// `sh`, `patch` → `diff`), and a best-effort guess for an untagged body.
[[nodiscard]] std::string family(std::string_view lang);
[[nodiscard]] std::string infer_family(std::string_view body);

[[nodiscard]] std::vector<std::string> split_table_row(std::string_view line);
[[nodiscard]] std::vector<std::string> table_headers(block const& b);
[[nodiscard]] std::vector<std::vector<std::string>> table_rows(block const& b);

// The command a code/command block carries: its first non-comment line, with a
// leading shell prompt (`$ `) stripped.
[[nodiscard]] std::string command_line(block const& b);
[[nodiscard]] std::optional<block> derive_command(block const& b);

[[nodiscard]] std::string display_lang(block const& b, bool guess);
[[nodiscard]] std::string nearest_heading(block const& b);
[[nodiscard]] std::string full_heading(block const& b);
[[nodiscard]] std::size_t count_lines(std::string_view text);
[[nodiscard]] std::string first_line(std::string_view text, std::size_t limit);

} // namespace prompt::extract
