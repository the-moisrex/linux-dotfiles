#include "../sdk/errors.hpp"
#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find('\n', begin);
        out.push_back(text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return out;
}

std::size_t indent_of(std::string_view line) {
    std::size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return n;
}

bool is_word_char(char c) {
    auto const uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_';
}

// Does `line` call/define `name` (`name(`, `name (`, `name = (`)?
bool calls(std::string_view line, std::string_view name) {
    if (name.empty()) return false;
    for (std::size_t pos = line.find(name); pos != std::string_view::npos; pos = line.find(name, pos + 1)) {
        if (pos > 0 && is_word_char(line[pos - 1])) continue;
        auto const end = pos + name.size();
        if (end < line.size() && is_word_char(line[end])) continue;
        auto j = end;
        while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j])) != 0) ++j;
        if (j < line.size() && line[j] == '=') {
            ++j;
            while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j])) != 0) ++j;
        }
        if (j < line.size() && line[j] == '(') return true;
    }
    return false;
}

bool looks_like_signature(std::string_view line) {
    auto const trimmed = trim_view(line);
    if (trimmed.empty() || trimmed.starts_with('#') || trimmed.starts_with("//")) return false;
    if (!trimmed.contains('(')) return false;
    static constexpr auto kControlKeywords =
        std::to_array<std::string_view>({"if", "for", "while", "switch", "catch", "return", "else", "do"});
    return std::ranges::none_of(kControlKeywords, [&](std::string_view k) {
        return trimmed.starts_with(k) && trimmed.size() > k.size() && !is_word_char(trimmed[k.size()]);
    });
}

std::string join_lines(std::vector<std::string_view> const& lines, std::size_t begin, std::size_t end) {
    std::string out;
    for (std::size_t i = begin; i <= end && i < lines.size(); ++i) {
        if (i != begin) out.push_back('\n');
        out += lines[i];
    }
    return out;
}

// Capture a brace-delimited definition starting at `start`.
std::size_t brace_end(std::vector<std::string_view> const& lines, std::size_t start) {
    int depth = 0;
    bool seen = false;
    for (std::size_t j = start; j < lines.size(); ++j) {
        for (char const c : lines[j]) {
            if (c == '{') {
                ++depth;
                seen = true;
            } else if (c == '}') {
                --depth;
            }
        }
        if (seen && depth <= 0) return j;
    }
    // A declaration (`int foo();`) has no braces: run to the semicolon.
    for (std::size_t j = start; j < lines.size() && j < start + 40; ++j) {
        if (trim_view(lines[j]).ends_with(';')) return j;
    }
    return start;
}

std::size_t python_end(std::vector<std::string_view> const& lines, std::size_t start) {
    auto const base = indent_of(lines[start]);
    std::size_t end = start;
    for (std::size_t j = start + 1; j < lines.size(); ++j) {
        if (trim_view(lines[j]).empty()) {
            end = j;
            continue;
        }
        if (indent_of(lines[j]) <= base) break;
        end = j;
    }
    return end;
}

std::vector<std::string> extract_cpp(std::vector<std::string_view> const& lines, std::string_view name) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < lines.size()) {
        bool const match = name == "*" ? looks_like_signature(lines[i]) : calls(lines[i], name);
        if (!match) {
            ++i;
            continue;
        }
        auto end = brace_end(lines, i);
        out.push_back(join_lines(lines, i, end));
        i = end + 1;
    }
    return out;
}

std::vector<std::string> extract_python(std::vector<std::string_view> const& lines, std::string_view name) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto const trimmed = trim_view(lines[i]);
        bool const definition =
            trimmed.starts_with("def ") || trimmed.starts_with("class ") || trimmed.starts_with("async def ");
        if (!definition) continue;
        if (name != "*" && !calls(lines[i], name)) continue;
        auto end = python_end(lines, i);
        while (end > i && trim_view(lines[end]).empty()) --end;
        out.push_back(join_lines(lines, i, end));
    }
    return out;
}

std::vector<match> run_function(std::string_view text, std::string_view qualifier) {
    auto const name = trim_view(qualifier);
    if (name.empty()) throw usage_error{"prompt: the function selector needs a name: function:main\n"};

    std::vector<block> sources;
    auto blocks = parse_document(text, "");
    for (auto const& b : blocks) {
        if (b.kind == "code") sources.push_back(b);
    }
    if (sources.empty()) {
        block whole;
        whole.kind = "code";
        whole.body = std::string(trim_view(text));
        whole.lang = infer_family(whole.body);
        sources.push_back(std::move(whole));
    }

    std::vector<match> out;
    for (auto const& b : sources) {
        auto const lines = split_lines(b.body);
        bool const python = b.lang == "python";
        auto const snippets = python ? extract_python(lines, name) : extract_cpp(lines, name);
        for (auto const& snippet : snippets) {
            match m;
            m.kind = "function";
            m.lang = b.lang;
            m.title = std::string(name);
            m.text = snippet;
            m.source = snippet;
            m.line = b.start_line;
            m.end_line = b.end_line;
            out.push_back(std::move(m));
        }
    }
    if (out.empty()) throw soft_error{std::format("no function `{}` found\n", name)};
    return out;
}

contract_text contract_function(std::string_view qualifier) {
    std::string const name = trim_view(qualifier).empty() ? "requested" : std::string(trim_view(qualifier));
    return make_contract(std::format("Return exactly one fenced code block containing only the `{}` function. "
                                     "Put no prose before or after the block.\n",
                                     name),
                         std::format("one fenced code block containing only the `{}` function", name));
}

} // namespace

bool const registered_function = register_extractor(extractor{
    .name = "function",
    .summary = "a function definition out of a code block; function:main, function:* for all",
    .run = &run_function,
    .contract = &contract_function,
});

} // namespace prompt::extract
