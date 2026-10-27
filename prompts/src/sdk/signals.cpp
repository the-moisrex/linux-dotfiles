#include "signals.hpp"
#include <algorithm>
#include <array>
#include <regex>
#include <unordered_set>
#include <utility>

namespace prompt::signals {

namespace {

// `[  FAILED  ] Suite.Test` — the test must carry a suite separator, exactly
// like run.cpp used to require before this parser moved here.
std::regex const& failed_re() {
    static std::regex const re(R"(^\[  FAILED  \] +([[:alnum:]_][^\s(),]*))");
    return re;
}

// `path:line:col: error:`/`path:line: error:`. Lazy on the path so a `:` inside
// a drive letter or a message does not swallow the location.
std::regex const& error_re() {
    static std::regex const re(R"(^(.+?):([0-9]+)(?::[0-9]+)?:\s*(?:fatal error|error|Error|ERROR)\b)");
    return re;
}

std::regex const& traceback_re() {
    static std::regex const re(R"x(File "([^"]+)", line ([0-9]+))x");
    return re;
}

void push_unique(std::vector<std::string>& out, std::unordered_set<std::string>& seen, std::string value) {
    if (!value.empty() && seen.insert(value).second) out.push_back(std::move(value));
}

} // namespace

intent intent_from(std::string_view name) {
    if (name == "fix") return intent::fix;
    if (name == "debug") return intent::debug;
    if (name == "review") return intent::review;
    return intent::general;
}

std::string_view to_string(intent value) {
    switch (value) {
    case intent::fix:
        return "fix";
    case intent::debug:
        return "debug";
    case intent::review:
        return "review";
    case intent::general:
        break;
    }
    return "general";
}

std::vector<std::string> failed_tests(std::string_view output) {
    std::vector<std::string> found;
    std::unordered_set<std::string> seen;
    std::size_t start = 0;
    while (start <= output.size()) {
        std::size_t end = output.find('\n', start);
        std::string_view line = output.substr(start, end == std::string_view::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        std::smatch match;
        std::string const line_str(line);
        if (std::regex_search(line_str, match, failed_re())) {
            std::string test = match[1];
            if (test.contains('.')) push_unique(found, seen, std::move(test));
        }

        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return found;
}

std::vector<std::string> extract_focus(std::string_view output) {
    std::vector<std::string> found;
    std::unordered_set<std::string> seen;

    std::size_t start = 0;
    while (start <= output.size()) {
        std::size_t end = output.find('\n', start);
        std::string_view line = output.substr(start, end == std::string_view::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        std::string const line_str(line);
        std::smatch match;
        if (std::regex_search(line_str, match, failed_re())) {
            std::string test = match[1];
            if (test.contains('.')) push_unique(found, seen, std::move(test));
        }
        if (std::regex_search(line_str, match, error_re())) {
            push_unique(found, seen, match[1].str() + ":" + match[2].str());
        }
        if (std::regex_search(line_str, match, traceback_re())) {
            push_unique(found, seen, match[1].str() + ":" + match[2].str());
        }

        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return found;
}

bool looks_like_failure(std::string_view text) {
    static constexpr std::array<std::string_view, 8> needles = {
        "[  FAILED  ]", "ninja: build stopped", "FAILED:", "Traceback (most recent call last)", "make: ***",
        ": error:",     ": fatal error:",       "error[E",
    };
    return std::ranges::any_of(needles, [&](std::string_view needle) { return text.contains(needle); });
}

std::vector<std::string> parse_focus(std::string_view value) {
    std::vector<std::string> items;
    std::size_t start = 0;
    while (start < value.size()) {
        std::size_t end = value.find('\n', start);
        std::string_view line = value.substr(start, end == std::string_view::npos ? end : end - start);
        if (!line.empty()) items.emplace_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return items;
}

std::string join_focus(std::span<std::string const> items) {
    std::string out;
    for (auto const& item : items) {
        if (item.empty()) continue;
        if (!out.empty()) out += '\n';
        out += item;
    }
    return out;
}

} // namespace prompt::signals
