#pragma once

// Small helpers shared by the extractors: comma lists, language-family
// matching, the `label: value` scanner `field`/`filename` use, and the
// contract-text builders.

#include "../sdk/extract.hpp"
#include "document.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

inline std::string_view trim_view(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return text;
}

inline std::vector<std::string> split_commas(std::string_view text) {
    std::vector<std::string> out;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find(',', begin);
        auto value = trim_view(text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin));
        if (!value.empty()) out.emplace_back(value);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return out;
}

// `--lang` compares families: `c++`/`cxx`/`hpp` mean `cpp`, `bash`/`console`
// mean `sh`, `patch` means `diff`. `any`/`untagged` select untagged blocks and
// `*` selects every language.
inline bool lang_matches(block const& b, std::string_view wanted, bool guess = false) {
    if (trim_view(wanted).empty()) return true;
    auto const values = split_commas(wanted);
    std::vector<std::string> families;
    for (auto const& value : values) {
        if (value == "*") return true;
        families.push_back(family(value));
    }
    if (b.lang.empty()) {
        for (auto const& f : families) {
            if (f == "any" || f == "untagged") return true;
        }
        if (guess) {
            auto const guessed = infer_family(b.body);
            for (auto const& f : families) {
                if (guessed == f) return true;
            }
        }
        return false;
    }
    return std::ranges::any_of(families, [&](std::string const& f) { return b.lang == f; });
}

struct field_hit {
    std::string value;
    std::size_t line;
};

inline std::string clean_field_value(std::string_view value) {
    auto text = std::string(trim_view(value));
    if (text.size() >= 2 && text.front() == text.back() &&
        (text.front() == '"' || text.front() == '\'' || text.front() == '`')) {
        text = std::string(trim_view(std::string_view(text).substr(1, text.size() - 2)));
    }
    return text;
}

// Every `label: value` line (case-insensitive label): `filename: x`,
// `filename = x`, or `- **filename**: x`.
inline std::vector<field_hit> find_fields(std::string_view text, std::string_view label) {
    auto const wanted = [&] {
        std::string out(label);
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }();
    std::vector<field_hit> hits;
    std::size_t begin = 0;
    std::size_t lineno = 0;
    while (begin <= text.size()) {
        ++lineno;
        auto const end = text.find('\n', begin);
        std::string_view line = text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin);
        line = trim_view(line);
        if (line.size() >= 2 && (line.front() == '-' || line.front() == '*') &&
            std::isspace(static_cast<unsigned char>(line[1])) != 0) {
            line = trim_view(line.substr(2));
        }
        if (line.starts_with("**")) line = line.substr(2);
        if (auto const sep = line.find_first_of(":="); sep != std::string_view::npos) {
            auto name = trim_view(line.substr(0, sep));
            if (name.ends_with("**")) name = trim_view(name.substr(0, name.size() - 2));
            bool const plain = !name.empty() && name.find_first_of("*:=") == std::string_view::npos;
            if (plain) {
                std::string lowered(name);
                for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lowered == wanted) {
                    auto value = clean_field_value(line.substr(sep + 1));
                    if (!value.empty()) hits.push_back(field_hit{.value = std::move(value), .line = lineno});
                }
            }
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return hits;
}

inline contract_text make_contract(std::string full, std::string clause) {
    return contract_text{.full = std::move(full), .clause = std::move(clause)};
}

} // namespace prompt::extract
