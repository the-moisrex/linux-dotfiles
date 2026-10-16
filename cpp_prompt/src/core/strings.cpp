#include "strings.hpp"

#include <charconv>

namespace prompt::text {

std::string rstrip_newlines(std::string_view s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.remove_suffix(1);
    return std::string(s);
}

std::string rstrip(std::string_view s, char c) {
    while (!s.empty() && s.back() == c) s.remove_suffix(1);
    return std::string(s);
}

std::string strip(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return rstrip(s);
}

std::vector<std::string> split_lines(std::string_view s, bool keep_blank) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find('\n', start);
        std::string_view line = s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (keep_blank || !line.empty()) out.emplace_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        std::size_t begin = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
        if (i > begin) out.emplace_back(s.substr(begin, i - begin));
    }
    return out;
}

std::string first_n_lines(std::string_view s, std::size_t n) {
    if (n == 0) return std::string(s);
    std::string out;
    std::size_t start = 0;
    std::size_t seen = 0;
    while (start < s.size() && seen < n) {
        std::size_t end = s.find('\n', start);
        std::size_t stop = end == std::string_view::npos ? s.size() : end + 1;
        out.append(s.substr(start, stop - start));
        ++seen;
        start = stop;
    }
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::size_t to_size(std::string_view s, std::size_t fallback) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string_view::npos) return fallback;
    std::size_t value = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), value);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) return fallback;
    return value;
}

long long to_ll(std::string_view s, long long fallback) {
    if (s.empty()) return fallback;
    bool negative = s.front() == '-';
    std::string_view digits = negative ? s.substr(1) : s;
    if (digits.find_first_not_of("0123456789") != std::string_view::npos) return fallback;
    long long value = 0;
    auto res = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (res.ec != std::errc{} || res.ptr != digits.data() + digits.size()) return fallback;
    return negative ? -value : value;
}

std::string regex_escape(std::string_view s) {
    static constexpr char const* meta = "\\^$.|?*+()[]{}";
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        for (char const* m = meta; *m != '\0'; ++m) {
            if (c == *m) {
                out += '\\';
                break;
            }
        }
        out += c;
    }
    return out;
}

} // namespace prompt::text
