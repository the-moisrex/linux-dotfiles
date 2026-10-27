#pragma once

#include "../sdk/project.hpp"
#include "../sdk/signals.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::compress {

// A fenced code block lifted from the accumulated context (context mode) or one
// file (source mode). Compressors only ever see a region, never the raw
// document, so a unit is testable on a plain string.
struct region {
    std::string path; // inferred source path ("" when unknown)
    std::string lang; // normalized fence info, e.g. "cpp" ("" when unknown)
    std::string text; // the body, exactly as it appeared
    std::size_t line = 1;
    bool focused = false; // never compress: the chain is about this file
};

// What a compressor may consult. `mode`/`focus` come from the `keys::mode` and
// `keys::focus` facts (or `.compress --for`); `level` rises as the planner
// tightens a budget.
struct signals {
    prompt::signals::intent mode = prompt::signals::intent::general;
    std::vector<std::string> focus;
    project::info project;
    int level = 1;
};

struct options {
    bool force = false; // compress even below min_lines
    std::optional<std::size_t> budget_lines;
    int level = 0;                 // 0 = auto (start at 1)
    std::size_t min_lines = 120;   // pass short regions through
    std::vector<std::string> only; // restrict to these compressor names
    std::vector<std::string> skip; // disable these compressor names
};

// What a compressor returns: the replacement markdown (without fences), a
// one-line legend naming what was dropped and how to expand it, and enough
// counts for the planner to reason about the saving.
struct outcome {
    std::string markdown;
    std::string legend;
    std::size_t kept = 0;
    std::size_t total = 0;
    bool changed = false;
};

using applies_fn = bool (*)(region const&, signals const&);
using compress_fn = outcome (*)(region const&, signals const&);

struct compressor {
    std::string_view name;    // "cpp.symbols"
    std::string_view summary; // one line for `.compress.list` and the AI menu
    applies_fn applies;       // does this unit claim the region?
    compress_fn compress;
    int priority = 0; // higher wins a tie; registration order breaks it
};

inline constexpr std::size_t compressor_capacity = 128;

bool register_compressor(compressor const& desc) noexcept;
[[nodiscard]] std::span<compressor const> get_all_compressors();
[[nodiscard]] compressor const* find_compressor(std::string_view name);

// A scanned document: literal markdown chunks and fenced regions, in order.
// `raw` is the exact original bytes, so an untouched piece round-trips and a
// compressed piece is rebuilt from `opening`/`marker`/the compressor output.
struct piece {
    std::string raw;
    bool fenced = false;
    std::string opening; // fenced: the opening fence line, e.g. "```cpp"
    std::string marker;  // fenced: the fence run, e.g. "```"
    region body;
};

[[nodiscard]] std::vector<piece> scan_markdown(std::string_view markdown);

struct report {
    std::string text;        // the rewritten document
    std::size_t before = 0;  // source lines
    std::size_t after = 0;   // result lines
    std::size_t regions = 0; // regions that were compressed
    bool changed = false;
};

// Context mode: compress the fenced regions of the accumulated Markdown.
[[nodiscard]] report compress_document(std::string_view markdown, signals base, options const& opt);

// Source mode: compress the given files, as if `.files` had embedded them.
[[nodiscard]] report compress_files(std::span<std::filesystem::path const> files, signals base, options const& opt);

// A dry run: one line per region, describing what the planner would do.
[[nodiscard]] std::vector<std::string> explain(std::string_view markdown, signals const& base, options const& opt);

// Helpers shared by the engine and the units.
[[nodiscard]] bool allowed(compressor const& desc, options const& opt);
[[nodiscard]] std::string fence_for(std::string_view lang);
[[nodiscard]] bool is_cpp_lang(std::string_view lang);
[[nodiscard]] std::size_t count_lines(std::string_view text);
[[nodiscard]] std::string plural(std::size_t n, std::string_view singular, std::string_view many);
[[nodiscard]] std::string first_items(std::span<std::string const> items, std::size_t limit);
[[nodiscard]] std::string normalize_whitespace(std::string_view text);

// The "> … Expand: `prompt .files <path>`" tail a legend ends with. `extra` is
// an alternative expansion (e.g. "`prompt gtest-case --exact Suite.Test`").
[[nodiscard]] std::string expand_hint(std::string_view path, std::string_view extra = {});

} // namespace prompt::compress
