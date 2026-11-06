#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/signals.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace prompt::prompts {

namespace {

constexpr std::string_view crlf = "\r\n";
constexpr std::string_view lf_only = "\n";

struct byte_match {
    std::size_t start{};
    std::size_t end{};
};

// ---- UTF-8 helpers ---------------------------------------------------------

// Offsets has one entry per decoded code point plus a final sentinel. Invalid
// input bytes use out-of-Unicode sentinels, so they cannot match valid code
// points; offsets still map each element to the original byte sequence.
struct cp_text {
    std::vector<char32_t> cps;
    std::vector<std::size_t> offsets;
};

[[nodiscard]]
constexpr bool is_utf8_continuation(unsigned char c) noexcept {
    return (c & 0xC0u) == 0x80u;
}

[[nodiscard]]
cp_text decode_utf8(std::string_view s) {
    cp_text out;
    out.cps.reserve(s.size());
    out.offsets.reserve(s.size() + 1);

    for (std::size_t i = 0; i < s.size();) {
        out.offsets.push_back(i);

        auto const lead = static_cast<unsigned char>(s[i]);
        char32_t cp{};
        std::size_t length{};

        if (lead < 0x80u) {
            cp = lead;
            length = 1;
        } else if ((lead & 0xE0u) == 0xC0u) {
            cp = lead & 0x1Fu;
            length = 2;
        } else if ((lead & 0xF0u) == 0xE0u) {
            cp = lead & 0x0Fu;
            length = 3;
        } else if ((lead & 0xF8u) == 0xF0u) {
            cp = lead & 0x07u;
            length = 4;
        }

        if (length == 0 || length > s.size() - i) [[unlikely]] {
            out.cps.push_back(0x110000u + lead);
            ++i;
            continue;
        }

        bool valid = true;
        for (std::size_t k = 1; k < length; ++k) {
            auto const continuation = static_cast<unsigned char>(s[i + k]);
            if (!is_utf8_continuation(continuation)) [[unlikely]] {
                valid = false;
                break;
            }
            cp = (cp << 6) | (continuation & 0x3Fu);
        }

        if (valid) {
            // Reject overlong encodings, UTF-16 surrogates, and values outside
            // the Unicode scalar range. Consume only the lead byte on failure,
            // matching the forgiving byte-by-byte fallback behavior.
            valid = (length != 2 || cp >= 0x80u) && (length != 3 || cp >= 0x800u) && (length != 4 || cp >= 0x10000u) &&
                    (cp < 0xD800u || cp > 0xDFFFu) && cp <= 0x10FFFFu;
        }

        if (!valid) [[unlikely]] {
            out.cps.push_back(0x110000u + lead);
            ++i;
            continue;
        }

        out.cps.push_back(cp);
        i += length;
    }

    out.offsets.push_back(s.size());
    return out;
}

[[nodiscard]]
constexpr char32_t normalize_cp(char32_t cp) noexcept {
    switch (cp) {
    case U'\u2018':
    case U'\u2019':
    case U'\u201A':
    case U'\u201B':
        return U'\'';
    case U'\u201C':
    case U'\u201D':
    case U'\u201E':
    case U'\u201F':
        return U'"';
    case U'\u2010':
    case U'\u2011':
    case U'\u2012':
    case U'\u2013':
    case U'\u2014':
    case U'\u2015':
    case U'\u2212':
        return U'-';
    case U'\u00A0':
        return U' ';
    default:
        break;
    }

    if (cp >= 0x2002u && cp <= 0x200Au) return U' ';
    if (cp == 0x202Fu || cp == 0x205Fu || cp == 0x3000) return U' ';
    return cp;
}

void encode_utf8(std::string& out, char32_t cp) {
    if (cp >= 0x110000u) {
        out += static_cast<char>(cp - 0x110000u);
    } else if (cp < 0x80u) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800u) {
        out += static_cast<char>(0xC0u | (cp >> 6));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        out += static_cast<char>(0xE0u | (cp >> 12));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (cp >> 18));
        out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    }
}

[[nodiscard]]
std::string normalize_for_match(std::string_view s) {
    auto const decoded = decode_utf8(s);

    std::string out;
    out.reserve(s.size());
    for (auto const cp : decoded.cps) {
        encode_utf8(out, normalize_cp(cp));
    }
    return out;
}

// ---- LF view / byte position bookkeeping ----------------------------------

// Collapses CRLF to LF and records the LF-view byte position at which each CR
// was removed. The vector is naturally sorted by construction.
struct lf_view {
    std::string text;
    std::vector<std::size_t> removed;
};

[[nodiscard]]
lf_view make_lf_view(std::string_view source) {
    lf_view view;
    view.text.reserve(source.size());

    std::size_t collapse_count{};
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (i + 1 < source.size() && source[i] == '\r' && source[i + 1] == '\n') {
            view.removed.push_back(i - collapse_count);
            ++collapse_count;
            view.text += '\n';
            ++i;
        } else {
            view.text += source[i];
        }
    }

    return view;
}

[[nodiscard]]
std::string normalize_newlines(std::string_view s) {
    std::string out;
    out.reserve(s.size());

    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i + 1 < s.size() && s[i] == '\r' && s[i + 1] == '\n') {
            out += '\n';
            ++i;
        } else {
            out += s[i];
        }
    }

    return out;
}

[[nodiscard]]
std::size_t count_before(std::span<std::size_t const> sorted, std::size_t pos) {
    return static_cast<std::size_t>(std::lower_bound(sorted.begin(), sorted.end(), pos) - sorted.begin());
}

[[nodiscard]]
std::size_t count_substr(std::string_view s, std::string_view sub) {
    std::size_t count{};
    std::size_t pos{};

    while ((pos = s.find(sub, pos)) != std::string_view::npos) {
        ++count;
        pos += sub.size();
    }

    return count;
}

[[nodiscard]]
std::string_view line_ending(std::string_view text, std::string_view fallback) {
    auto const crlf_count = count_substr(text, crlf);
    auto const total_lf = count_substr(text, "\n");
    auto const lf_count = total_lf - crlf_count;

    if (crlf_count == lf_count) return fallback;
    return crlf_count > lf_count ? crlf : lf_only;
}

[[nodiscard]]
std::string_view trim_end(std::string_view s) noexcept {
    while (!s.empty()) {
        switch (s.back()) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case '\v':
        case '\f':
            s.remove_suffix(1);
            break;
        default:
            return s;
        }
    }
    return s;
}

// ---- Exact and typographic tier -------------------------------------------

[[nodiscard]]
std::vector<byte_match> find_occurrences(std::string_view content, std::string_view search) {
    std::vector<byte_match> matches;

    if (search.empty() || search.size() > content.size()) [[unlikely]] {
        return matches;
    }

    for (std::size_t pos = 0; (pos = content.find(search, pos)) != std::string_view::npos;) {
        matches.push_back({.start = pos, .end = pos + search.size()});
        // Deliberately preserve the original non-overlapping exact-match
        // semantics.
        pos += search.size();
    }

    return matches;
}

[[nodiscard]]
std::vector<byte_match> find_typographic(std::string_view lf_text, std::string_view search) {
    auto const decoded_text = decode_utf8(lf_text);
    auto const decoded_search = decode_utf8(search);

    if (decoded_search.cps.empty() || decoded_search.cps.size() > decoded_text.cps.size()) [[unlikely]] {
        return {};
    }

    std::vector<char32_t> normalized_text;
    normalized_text.reserve(decoded_text.cps.size());
    for (auto const cp : decoded_text.cps) {
        normalized_text.push_back(normalize_cp(cp));
    }

    std::vector<char32_t> normalized_search;
    normalized_search.reserve(decoded_search.cps.size());
    for (auto const cp : decoded_search.cps) {
        normalized_search.push_back(normalize_cp(cp));
    }

    std::vector<byte_match> matches;
    for (std::size_t i = 0; i + normalized_search.size() <= normalized_text.size(); ++i) {
        bool equal = true;

        for (std::size_t k = 0; k < normalized_search.size(); ++k) {
            if (normalized_text[i + k] != normalized_search[k]) {
                equal = false;
                break;
            }
        }

        if (equal) {
            matches.push_back(
                {.start = decoded_text.offsets[i], .end = decoded_text.offsets[i + normalized_search.size()]});
            i += normalized_search.size() - 1; // Keep matches non-overlapping.
        }
    }

    return matches;
}

// ---- Trailing-whitespace tier ---------------------------------------------

struct line_span {
    std::size_t start{};
    std::size_t end{};         // Includes the line ending, if present.
    std::size_t content_end{}; // Excludes CRLF or LF.
    std::string_view text;     // Views the original content.
    bool has_newline{};
};

[[nodiscard]]
std::vector<line_span> split_lines(std::string_view content) {
    std::vector<line_span> lines;
    if (content.empty()) [[unlikely]]
        return lines;

    auto const newline_count = static_cast<std::size_t>(std::count(content.begin(), content.end(), '\n'));
    lines.reserve(newline_count + 1);

    for (std::size_t pos = 0; pos < content.size();) {
        auto const eol = content.find('\n', pos);

        if (eol == std::string_view::npos) {
            lines.push_back({.start = pos,
                             .end = content.size(),
                             .content_end = content.size(),
                             .text = content.substr(pos),
                             .has_newline = false});
            break;
        }

        auto const raw_line = content.substr(pos, eol - pos);
        bool const has_cr = !raw_line.empty() && raw_line.back() == '\r';

        lines.push_back({.start = pos,
                         .end = eol + 1,
                         .content_end = eol - static_cast<std::size_t>(has_cr),
                         .text = has_cr ? raw_line.substr(0, raw_line.size() - 1) : raw_line,
                         .has_newline = true});

        pos = eol + 1;
    }

    return lines;
}

[[nodiscard]]
std::vector<std::string_view> split_search(std::string_view search, bool& trailing_newline) {
    std::vector<std::string_view> lines;
    std::size_t pos{};

    while (true) {
        auto const eol = search.find('\n', pos);
        if (eol == std::string_view::npos) {
            lines.emplace_back(search.substr(pos));
            break;
        }

        lines.emplace_back(search.substr(pos, eol - pos));
        pos = eol + 1;
    }

    trailing_newline = !search.empty() && search.back() == '\n';
    if (trailing_newline && !lines.empty()) {
        lines.pop_back();
    }

    return lines;
}

[[nodiscard]]
std::string normalized_line_key(std::string_view line) {
    auto const normalized = normalize_for_match(line);
    return std::string{trim_end(normalized)};
}

[[nodiscard]]
std::vector<byte_match> find_line_occurrences(std::string_view content, std::string_view search) {
    bool trailing_newline{};
    auto const expected = split_search(search, trailing_newline);
    auto const lines = split_lines(content);

    if (expected.empty() || expected.size() > lines.size()) [[unlikely]] {
        return {};
    }

    // Normalize each line once. The previous nested comparison repeatedly
    // allocated and normalized the same source lines.
    std::vector<std::string> expected_keys;
    expected_keys.reserve(expected.size());
    for (auto const line : expected) {
        expected_keys.push_back(normalized_line_key(line));
    }

    std::vector<std::string> line_keys;
    line_keys.reserve(lines.size());
    for (auto const& line : lines) {
        line_keys.push_back(normalized_line_key(line.text));
    }

    std::vector<byte_match> matches;
    for (std::size_t i = 0; i + expected.size() <= lines.size(); ++i) {
        bool equal = true;

        for (std::size_t k = 0; k < expected.size(); ++k) {
            if (line_keys[i + k] != expected_keys[k]) {
                equal = false;
                break;
            }
        }

        if (!equal) continue;

        auto const& last = lines[i + expected.size() - 1];
        if (trailing_newline && !last.has_newline) continue;

        auto const end = trailing_newline ? last.end : last.content_end;
        byte_match const candidate{.start = lines[i].start, .end = end};

        // Candidates are generated in ascending start/end order. Only the
        // immediately preceding accepted interval can overlap this candidate.
        if (matches.empty() || matches.back().end <= candidate.start) {
            matches.push_back(candidate);
        }
    }

    return matches;
}

// ---- Line-ending-preserving replacement ----------------------------------

[[nodiscard]]
std::vector<std::optional<std::size_t>> align_lines(std::span<std::string const> before,
                                                    std::span<std::string const> after) {
    using cell_type = std::uint32_t;

    if (before.size() >= std::numeric_limits<cell_type>::max() || after.size() >= std::numeric_limits<cell_type>::max())
        [[unlikely]] {
        throw soft_error{"Replacement region has too many lines to align.\n"};
    }

    auto const rows = before.size() + 1;
    auto const width = after.size() + 1;

    std::vector<cell_type> table;
    if (width != 0 && rows > table.max_size() / width) [[unlikely]] {
        throw soft_error{"Replacement region is too large to align.\n"};
    }
    table.assign(rows * width, cell_type{});

    for (std::size_t i = before.size(); i-- > 0;) {
        for (std::size_t j = after.size(); j-- > 0;) {
            auto& cell = table[(i * width) + j];
            if (before[i] == after[j]) {
                cell = static_cast<cell_type>(table[((i + 1) * width) + (j + 1)] + 1u);
            } else {
                cell = std::max(table[((i + 1) * width) + j], table[(i * width) + (j + 1)]);
            }
        }
    }

    std::vector<std::optional<std::size_t>> result(after.size());
    std::vector<std::size_t> gap_before;
    std::vector<std::size_t> gap_after;

    auto flush = [&]() {
        auto const count = std::min(gap_before.size(), gap_after.size());
        for (std::size_t k = 0; k < count; ++k) {
            result[gap_after[k]] = gap_before[k];
        }
        gap_before.clear();
        gap_after.clear();
    };

    std::size_t i{};
    std::size_t j{};
    while (i < before.size() && j < after.size()) {
        if (before[i] == after[j]) {
            flush();
            result[j++] = i++;
        } else if (table[((i + 1) * width) + j] >= table[(i * width) + (j + 1)]) {
            // Keep the original tie-break: prefer consuming a before-line.
            gap_before.push_back(i++);
        } else {
            gap_after.push_back(j++);
        }
    }

    while (i < before.size()) gap_before.push_back(i++);
    while (j < after.size()) gap_after.push_back(j++);
    flush();

    return result;
}

[[nodiscard]]
std::string with_region_endings(std::string_view region, std::string_view replacement, std::string_view fallback) {
    std::vector<std::string_view> before;
    std::vector<std::string_view> endings;

    for (std::size_t pos = 0;;) {
        auto const eol = region.find('\n', pos);
        if (eol == std::string_view::npos) {
            before.emplace_back(region.substr(pos));
            break;
        }

        auto end_pos = eol;
        auto ending = region.substr(eol, 1);
        if (eol > pos && region[eol - 1] == '\r') {
            end_pos = eol - 1;
            ending = region.substr(eol - 1, 2);
        }

        before.emplace_back(region.substr(pos, end_pos - pos));
        endings.emplace_back(ending);
        pos = eol + 1;

        if (pos == region.size()) {
            before.emplace_back(std::string_view{});
            break;
        }
    }

    std::vector<std::string_view> after;
    for (std::size_t pos = 0;;) {
        auto const eol = replacement.find('\n', pos);
        if (eol == std::string_view::npos) {
            after.emplace_back(replacement.substr(pos));
            break;
        }

        after.emplace_back(replacement.substr(pos, eol - pos));
        pos = eol + 1;
    }

    std::vector<std::string> before_keys;
    before_keys.reserve(before.size());
    for (auto const line : before) {
        before_keys.push_back(normalized_line_key(line));
    }

    std::vector<std::string> after_keys;
    after_keys.reserve(after.size());
    for (auto const line : after) {
        after_keys.push_back(normalized_line_key(line));
    }

    auto const aligned = align_lines(before_keys, after_keys);

    std::string out;
    out.reserve(replacement.size());

    for (std::size_t idx = 0; idx < after.size(); ++idx) {
        out.append(after[idx]);
        if (idx + 1 == after.size()) break;

        auto ending = fallback;
        if (aligned[idx] && *aligned[idx] < endings.size()) {
            ending = endings[*aligned[idx]];
        }
        out.append(ending);
    }

    return out;
}

// ---- The view-matching pipeline (mirrors the JS `matchView`) ---------------

[[nodiscard]]
std::vector<byte_match> match_view(std::string_view source, std::string_view old_string) {
    auto const lf = make_lf_view(source);
    auto const search = normalize_newlines(old_string);

    std::vector<byte_match> matches;
    if (auto exact = find_occurrences(lf.text, search); !exact.empty()) {
        matches = std::move(exact);
    } else if (auto typographic = find_typographic(lf.text, search); !typographic.empty()) {
        matches = std::move(typographic);
    } else {
        matches = find_line_occurrences(lf.text, search);
    }

    for (auto& match : matches) {
        match.start += count_before(lf.removed, match.start);
        match.end += count_before(lf.removed, match.end);
    }

    return matches;
}

// ---- The miss hint (mirrors the JS `missHint`) -----------------------------

[[nodiscard]]
std::string miss_hint(std::string_view source, std::string_view old_string, std::string_view new_string) {
    auto const view = normalize_newlines(source);
    auto const replacement = normalize_newlines(new_string);

    // #1: newString is already in the file.
    {
        auto const trimmed = trim_end(replacement);
        auto trimmed_front = trimmed;
        while (!trimmed_front.empty() && (trimmed_front.front() == ' ' || trimmed_front.front() == '\t')) {
            trimmed_front.remove_prefix(1);
        }

        auto const normalized_view = normalize_for_match(view);
        auto const normalized_replacement = normalize_for_match(replacement);
        if (trimmed_front.size() >= 20 && normalized_view.find(normalized_replacement) != std::string::npos) {
            return "newString is already in the file, so this edit may already be applied.";
        }
    }

    auto distinct = [](std::string_view line) {
        for (char ch : line) {
            auto const c = static_cast<unsigned char>(ch);
            if (std::isalnum(c) != 0 || (c & 0x80u) != 0) return true;
        }
        return false;
    };

    std::vector<std::string_view> lines;
    for (std::size_t pos = 0;;) {
        auto const eol = view.find('\n', pos);
        if (eol == std::string_view::npos) {
            lines.emplace_back(std::string_view{view}.substr(pos));
            break;
        }
        lines.emplace_back(std::string_view{view}.substr(pos, eol - pos));
        pos = eol + 1;
    }

    std::vector<std::string_view> search_lines;
    for (std::size_t pos = 0;;) {
        auto const eol = old_string.find('\n', pos);
        if (eol == std::string_view::npos) {
            search_lines.emplace_back(old_string.substr(pos));
            break;
        }
        search_lines.emplace_back(old_string.substr(pos, eol - pos));
        pos = eol + 1;
    }

    std::vector<std::string> line_keys;
    line_keys.reserve(lines.size());
    for (auto const line : lines) {
        line_keys.push_back(normalized_line_key(line));
    }

    std::vector<std::string> search_keys;
    search_keys.reserve(search_lines.size());
    for (auto const line : search_lines) {
        search_keys.push_back(normalized_line_key(line));
    }

    std::unordered_map<std::string, std::vector<std::size_t>> positions;
    positions.reserve(lines.size());
    for (std::size_t i = 0; i < line_keys.size(); ++i) {
        if (!distinct(line_keys[i])) continue;
        positions[line_keys[i]].push_back(i);
    }

    std::map<std::ptrdiff_t, std::size_t> votes;
    std::size_t wanted{};
    for (std::size_t offset = 0; offset < search_keys.size(); ++offset) {
        auto const& key = search_keys[offset];
        if (!distinct(key)) continue;
        ++wanted;

        auto const found = positions.find(key);
        if (found == positions.end()) continue;

        for (auto const index : found->second) {
            votes[static_cast<std::ptrdiff_t>(index) - static_cast<std::ptrdiff_t>(offset)]++;
        }
    }

    std::ptrdiff_t best_start{};
    std::size_t best_count{};
    for (auto const& [start, count] : votes) {
        if (count > best_count) {
            best_start = start;
            best_count = count;
        }
    }

    auto const minimum_votes = (wanted / 2) + (wanted % 2);
    if (best_count == 0 || best_count < minimum_votes) [[unlikely]] {
        return {};
    }

    std::vector<std::size_t> matched;
    for (std::size_t offset = 0; offset < search_keys.size(); ++offset) {
        auto const& key = search_keys[offset];
        auto const index = best_start + static_cast<std::ptrdiff_t>(offset);

        if (!key.empty() && index >= 0 && static_cast<std::size_t>(index) < line_keys.size() &&
            line_keys[static_cast<std::size_t>(index)] == key) {
            matched.push_back(static_cast<std::size_t>(index));
        }
    }

    if (matched.empty()) [[unlikely]]
        return {};

    constexpr std::size_t hint_lines = 12;
    constexpr std::size_t hint_line_length = 200;

    auto const from = matched.front();
    auto const available = lines.size() - from;
    auto const to = std::min(matched.back() + 1, from + std::min(hint_lines, available));

    std::string hint = "The closest lines in the file:";
    for (std::size_t i = from; i < to; ++i) {
        auto const line = lines[i];
        auto const shown = line.size() > hint_line_length
                               ? std::string{line.substr(0, hint_line_length)} + "\xE2\x80\xA6"
                               : std::string{line};

        hint += '\n';
        hint += std::to_string(i + 1);
        hint += ": ";
        hint += shown;
    }

    return hint;
}

// ---- Diff output -----------------------------------------------------------

[[nodiscard]]
std::string unified_diff(std::string_view path, std::string_view before, std::string_view after) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto const base = fs::temp_directory_path(ec);
    if (ec) throw soft_error{"Cannot locate temporary directory: " + ec.message() + "\n"};
    static std::atomic_uint64_t sequence{};
    auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path directory;
    bool created = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        directory = base / ("prompt-diff-" + std::to_string(stamp) + "-" +
                            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        ec.clear();
        if (fs::create_directory(directory, ec)) {
            created = true;
            break;
        }
        if (ec && ec != std::errc::file_exists)
            throw soft_error{"Cannot create temporary diff directory: " + ec.message() + "\n"};
    }
    if (!created) throw soft_error{"Cannot create temporary diff directory.\n"};

    struct cleanup_guard {
        fs::path const& path;
        ~cleanup_guard() noexcept {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{directory};

    fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) throw soft_error{"Cannot secure temporary diff directory: " + ec.message() + "\n"};

    auto write_temp = [](fs::path const& file, std::string_view content) {
        if (content.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            throw soft_error{"Diff input is too large.\n"};
        std::ofstream out(file, std::ios::binary);
        if (!out) throw soft_error{"Cannot open diff input.\n"};
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out) throw soft_error{"Cannot write diff input.\n"};
    };

    auto const old_file = directory / "old";
    auto const new_file = directory / "new";
    write_temp(old_file, before);
    write_temp(new_file, after);
    auto result = prompt::process::run_script(R"(diff -u --label "a/$3" --label "b/$3" -- "$1" "$2")",
                                              {old_file.string(), new_file.string(), std::string{path}}, {},
                                              std::chrono::seconds{10});
    return result.stdout_data;
}

// ---- File reading and writing ---------------------------------------------

[[nodiscard]]
std::string strip_cr(std::string_view s) {
    std::string out{s};
    std::erase(out, '\r');
    return out;
}

[[nodiscard]]
std::string read_checked(std::string const& path) {
    std::error_code ec;

    auto const exists = std::filesystem::exists(path, ec);
    if (ec) {
        throw soft_error{"Cannot access " + path + ": " + ec.message() + "\n"};
    }
    if (!exists) [[unlikely]] {
        throw soft_error{"File not found: " + path + "\n"};
    }

    auto const is_directory = std::filesystem::is_directory(path, ec);
    if (ec) {
        throw soft_error{"Cannot inspect " + path + ": " + ec.message() + "\n"};
    }
    if (is_directory) [[unlikely]] {
        throw soft_error{"Path is a directory, not a file: " + path + "\n"};
    }

    return read_file(path);
}

// Write in a sibling temporary directory, then rename the completed file over
// the destination. This avoids truncating the original if writing fails. The
// replacement is atomic on the usual local POSIX/Windows filesystems. Basic
// permission bits are retained; ACLs, extended attributes, and hard-link
// relationships are not preserved by replacing an inode.
void write_file_atomically(std::string const& path, std::string_view contents) {
    namespace fs = std::filesystem;

    if (contents.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) [[unlikely]] {
        throw soft_error{"Replacement file is too large to write: " + path + "\n"};
    }

    std::error_code ec;
    auto destination = fs::canonical(fs::path{path}, ec);
    if (ec) {
        throw soft_error{"Cannot resolve " + path + ": " + ec.message() + "\n"};
    }

    auto const original_status = fs::status(destination, ec);
    if (ec) {
        throw soft_error{"Cannot inspect " + path + ": " + ec.message() + "\n"};
    }
    auto const original_permissions = original_status.permissions();

    static std::atomic_uint64_t sequence{};
    auto const timestamp = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

    fs::path temporary_directory;
    bool created{};
    for (int attempt = 0; attempt < 32; ++attempt) {
        temporary_directory = destination;
        temporary_directory += ".tmp.";
        temporary_directory += std::to_string(timestamp);
        temporary_directory += ".";
        temporary_directory += std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));

        ec.clear();
        if (fs::create_directory(temporary_directory, ec)) {
            created = true;
            break;
        }
        if (ec && ec != std::errc::file_exists) {
            throw soft_error{"Cannot create temporary file for " + path + ": " + ec.message() + "\n"};
        }
    }

    if (!created) [[unlikely]] {
        throw soft_error{"Cannot create a unique temporary file for " + path + ".\n"};
    }

    auto const temporary_file = temporary_directory / "replacement";
    struct cleanup_guard {
        fs::path const& file;
        fs::path const& directory;

        ~cleanup_guard() noexcept {
            std::error_code ignored;
            fs::remove(file, ignored);
            fs::remove(directory, ignored);
        }
    } cleanup{temporary_file, temporary_directory};

    {
        std::ofstream output(temporary_file, std::ios::binary | std::ios::out);
        if (!output) [[unlikely]] {
            throw soft_error{"Cannot open temporary file for " + path + " for writing.\n"};
        }

        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output) [[unlikely]] {
            throw soft_error{"Failed writing temporary file for " + path + ".\n"};
        }

        output.close();
        if (!output) [[unlikely]] {
            throw soft_error{"Failed closing temporary file for " + path + ".\n"};
        }
    }

    if (original_permissions != fs::perms::unknown) {
        fs::permissions(temporary_file, original_permissions, fs::perm_options::replace, ec);
        if (ec) [[unlikely]] {
            throw soft_error{"Cannot preserve permissions while editing " + path + ": " + ec.message() + "\n"};
        }
    }

#ifdef _WIN32
    if (!::MoveFileExW(temporary_file.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        [[unlikely]] {
        auto const error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        throw soft_error{"Cannot replace " + path + ": " + error.message() + "\n"};
    }
#else
    fs::rename(temporary_file, destination, ec);
    if (ec) [[unlikely]] {
        throw soft_error{"Cannot replace " + path + ": " + ec.message() + "\n"};
    }
#endif
}

template <typename Replace>
[[nodiscard]]
std::string apply_replacements(std::string_view source, std::span<byte_match const> matches, Replace&& replace) {
    std::string result{source};

    // Apply from the end so earlier byte offsets remain valid.
    for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
        auto replacement = std::invoke(replace, *it);
        result.replace(it->start, it->end - it->start, replacement);
    }

    return result;
}

} // namespace

static void execute_edit(view const& v, result& r) {
    // Validates --head and accepts --num/--numbered for consistency with the
    // other file prompts; --head doubles as the scan limit below.
    auto const opts = file_options_or_throw(v);

    std::string path;
    std::optional<std::string> old_string;
    std::optional<std::string> new_string;
    std::optional<std::string> old_file;
    std::optional<std::string> new_file;
    bool replace_all{};
    bool dry_run{};
    bool show_diff{};
    bool path_set{};

    std::size_t i{};
    while (i < v.args.size()) {
        auto const arg = v.args[i];

        if (arg == "--head") {
            i += 2;
            continue;
        }
        if (is_number_flag(arg)) {
            ++i;
            continue;
        }
        if (arg == "--old") {
            if (i + 1 >= v.args.size()) [[unlikely]] {
                throw usage_error{"--old requires a value.\n"};
            }
            old_string = std::string{v.args[i + 1]};
            i += 2;
            continue;
        }
        if (arg == "--new") {
            if (i + 1 >= v.args.size()) [[unlikely]] {
                throw usage_error{"--new requires a value.\n"};
            }
            new_string = std::string{v.args[i + 1]};
            i += 2;
            continue;
        }
        if (arg == "--old-file") {
            if (i + 1 >= v.args.size()) [[unlikely]] {
                throw usage_error{"--old-file requires a path.\n"};
            }
            old_file = std::string{v.args[i + 1]};
            i += 2;
            continue;
        }
        if (arg == "--new-file") {
            if (i + 1 >= v.args.size()) [[unlikely]] {
                throw usage_error{"--new-file requires a path.\n"};
            }
            new_file = std::string{v.args[i + 1]};
            i += 2;
            continue;
        }
        if (arg == "--all" || arg == "--replace-all") {
            replace_all = true;
            ++i;
            continue;
        }
        if (arg == "--diff") {
            show_diff = true;
            ++i;
            continue;
        }
        if (arg == "--dry-run") {
            dry_run = true;
            ++i;
            continue;
        }
        if (arg.size() > 1 && arg.front() == '-') [[unlikely]] {
            throw usage_error{"Unknown option: " + std::string{arg} + "\n"};
        }
        if (path_set) [[unlikely]] {
            throw usage_error{"Only one <path> may be given.\n"};
        }

        path = std::string{arg};
        path_set = true;
        ++i;
    }

    if (!path_set) [[unlikely]] {
        throw usage_error{"Missing <path>.\n"};
    }
    if (old_string && old_file) [[unlikely]] {
        throw usage_error{"--old and --old-file are mutually exclusive.\n"};
    }
    if (new_string && new_file) [[unlikely]] {
        throw usage_error{"--new and --new-file are mutually exclusive.\n"};
    }
    if (!old_string && !old_file) [[unlikely]] {
        throw usage_error{"Provide --old <text> or --old-file <path>.\n"};
    }
    if (!new_string && !new_file) [[unlikely]] {
        throw usage_error{"Provide --new <text> or --new-file <path>.\n"};
    }
    if (old_file && *old_file == "-" && new_file && *new_file == "-") [[unlikely]] {
        throw usage_error{"Only one of --old-file - / --new-file - can read stdin.\n"};
    }

    auto read_arg = [&](std::optional<std::string> const& inline_text, std::optional<std::string> const& from_path,
                        std::string_view what) -> std::string {
        if (inline_text) return *inline_text;

        // The option validation above guarantees from_path is engaged here.
        if (*from_path == "-") {
            if (v.in.spent() || !v.in.piped()) [[unlikely]] {
                throw usage_error{"--" + std::string{what} + "-file - requires piped stdin.\n"};
            }

            std::string text{v.in.text()};
            v.in.spend();
            return text;
        }

        return read_checked(*from_path);
    };

    auto const old_text = read_arg(old_string, old_file, "old");
    auto const new_text = read_arg(new_string, new_file, "new");

    if (old_text.empty()) [[unlikely]] {
        throw usage_error{"oldString must not be empty; provide a non-empty anchor.\n"};
    }
    if (old_text == new_text) [[unlikely]] {
        throw usage_error{"oldString and newString are identical; nothing to do.\n"};
    }

    auto const source = read_checked(path);

    // --head limits matching to the first N newline-terminated lines.
    std::string_view scan_view{source};
    if (opts.head_lines) {
        std::size_t line_count{};
        std::size_t pos{};

        while (pos < source.size() && line_count < *opts.head_lines) {
            if (source[pos] == '\n') ++line_count;
            ++pos;
        }

        scan_view = scan_view.substr(0, pos);
    }

    // A pure CR-removal/addition edit intentionally uses raw-byte matching so
    // callers can explicitly change line-ending bytes.
    bool const endings_only = strip_cr(old_text) == strip_cr(new_text);

    std::vector<byte_match> matches;
    if (endings_only) {
        matches = find_occurrences(scan_view, old_text);
    } else {
        matches = match_view(scan_view, old_text);
    }

    if (matches.empty()) [[unlikely]] {
        auto const hint = miss_hint(scan_view, old_text, new_text);
        std::string message = "Could not find oldString in " + path + ".";
        if (!hint.empty()) {
            message += "\n\n";
            message += hint;
        }
        message += '\n';
        throw soft_error{std::move(message)};
    }

    if (matches.size() > 1 && !replace_all) [[unlikely]] {
        throw soft_error{"Found " + std::to_string(matches.size()) +
                         " matches for oldString, but expected exactly one. "
                         "Add more surrounding context, or pass --all to replace every occurrence.\n"};
    }

    std::string replaced;
    if (endings_only) {
        replaced = apply_replacements(source, matches, [&](byte_match const&) { return new_text; });
    } else {
        auto const normalized_replacement = normalize_newlines(new_text);
        auto const file_ending = line_ending(source, lf_only);

        replaced = apply_replacements(source, matches, [&](byte_match const& match) {
            auto const region = std::string_view{source}.substr(match.start, match.end - match.start);

            return with_region_endings(region, normalized_replacement, line_ending(region, file_ending));
        });
    }

    if (replaced == source) [[unlikely]] {
        throw soft_error{endings_only ? "No changes to apply: " + path + " already has those line endings.\n"
                                      : "No changes to apply: the edit leaves " + path + " unchanged.\n"};
    }

    auto const replacements = matches.size();

    if (!dry_run) {
        write_file_atomically(path, replaced);
    }

    std::string status = dry_run ? "[dry-run] " : "";
    status += "Edited ";
    status += path;
    status += " (";
    status += std::to_string(replacements);
    status += " replacement";
    if (replacements != 1) status += 's';
    status += ")\n";

    if (show_diff) {
        auto diff = unified_diff(path, source, replaced);
        if (!diff.empty()) {
            status += '\n';
            status += diff;
        }
    }

    r.out = std::move(status);
    r.no_clipboard = true;
}

static void render_help_edit(std::ostream& os) {
    os << file_usage_help("edit", " --old <text> --new <text> [--all] [--diff] [--dry-run]") << R"EOF(
Find exact text in a file and replace it. The file is edited in place.

Matching: exact first, then typographic variants (curly quotes, dashes, non-
breaking spaces), then line-wise trailing-whitespace tolerance. Line endings
(CRLF/LF) are preserved. To change line endings themselves, make --old and
--new differ only in \r.

Without --all, the old text must appear exactly once. With --all, every
occurrence is replaced.

Options:
  --old <text>             The text to find (must not be empty).
  --new <text>             The replacement text.
  --old-file <path>        Read the old text from a file (`-` = stdin).
  --new-file <path>        Read the new text from a file (`-` = stdin).
  --all, --replace-all     Replace every occurrence, not just a unique one.
  --diff                   Also print a unified diff of the change.
  --dry-run                Scan and report, but do not write the file.
)EOF" << file_options_help("the scan region (--head limits how many lines are searched)");
}

bool const registered =
    prompt::register_prompt("edit",
                            "Find exact text in a file and replace it; --old/--new or --old-file/--new-file, "
                            "--all for every occurrence, --diff to show the change.",
                            &execute_edit, &render_help_edit);

} // namespace prompt::prompts
