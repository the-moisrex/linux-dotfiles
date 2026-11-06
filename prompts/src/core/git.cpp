#include "./git.hpp"

#include "process.hpp"
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace detail {

// Tiny shell-style glob matcher supporting '*' and '?'.
inline bool glob_match(std::string_view pattern, std::string_view text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star_p = std::string_view::npos;
    std::size_t star_t = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_t = t;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            t = ++star_t;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

inline std::string_view strip_quotes(std::string_view path) {
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        path.remove_prefix(1);
        path.remove_suffix(1);
    }
    return path;
}

// Extract the destination path from a "diff --git a/<old> b/<new>" header.
inline std::string path_from_diff_header(std::string_view line) {
    if (auto pos = line.rfind(" b/"); pos != std::string_view::npos)
        return std::string(strip_quotes(line.substr(pos + 3)));

    // Quoted form:  diff --git "a/foo bar" "b/foo bar"
    if (auto q = line.rfind("\"b/"); q != std::string_view::npos) {
        auto p = line.substr(q + 1);
        if (!p.empty() && p.back() == '"') p.remove_suffix(1);
        if (p.starts_with("b/")) p.remove_prefix(2);
        return std::string(p);
    }
    return {};
}

} // namespace detail

// Run a git command and return stdout with trailing newlines stripped.
std::string prompt::git::run(std::vector<std::string> const& args) {
    auto result = prompt::process::run_command(args);
    std::string out = std::move(result.stdout_data);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::string prompt::git::run(std::initializer_list<char const*> args) {
    std::vector<std::string> v;
    v.reserve(args.size());
    for (auto const* a : args) v.emplace_back(a);
    return run(v);
}

// Annotate ordinary unified diffs using destination-file line numbers.
// Combined diffs (multiple prefix columns) are left unchanged.
std::string prompt::git::add_line_numbers_to_diff(std::string_view diff) {
    std::istringstream stream{std::string(diff)};
    std::string line;
    std::string result;

    unsigned long long new_line_num = 0;
    bool in_hunk = false;

    while (std::getline(stream, line)) {
        if (line.starts_with("diff ")) {
            in_hunk = false;
            result += line;
            result.push_back('\n');
            continue;
        }

        if (line.starts_with("@@")) {
            in_hunk = false;
            if (line.starts_with("@@ -")) {
                auto pos = line.find(" +");
                if (pos != std::string::npos) {
                    try {
                        new_line_num = std::stoull(line.substr(pos + 2));
                        in_hunk = true;
                    } catch (...) {
                        // Preserve malformed headers without annotation.
                    }
                }
            }
            result += line;
            result.push_back('\n');
            continue;
        }

        if (!in_hunk) {
            result += line;
            result.push_back('\n');
            continue;
        }
        if (line.empty()) {
            result += std::format("{:5d}   \n", new_line_num++);
            continue;
        }

        char prefix = line[0];
        if (prefix == '+') {
            result += std::format("{:5d} + {}", new_line_num++, line.substr(1));
        } else if (prefix == '-') {
            result += std::format("      - {}", line.substr(1));
        } else if (prefix == ' ') {
            result += std::format("{:5d}   {}", new_line_num++, line.substr(1));
        } else {
            result += line;
        }
        result.push_back('\n');
    }

    return result;
}

// Keep/drop whole-file blocks of a unified diff.
//   * `includes` non-empty => only blocks whose path matches one of them are kept.
//   * `excludes`          => blocks whose path matches any of them are dropped.
// Paths are matched both verbatim and by basename, using `detail::glob_match`.
std::string prompt::git::filter_diff_by_path(std::string_view diff, std::vector<std::string> const& includes,
                                             std::vector<std::string> const& excludes) {
    if (includes.empty() && excludes.empty()) return std::string(diff);

    auto matches = [](std::string_view path, std::vector<std::string> const& patterns) {
        if (path.empty()) return false;
        for (auto const& pat : patterns) {
            if (detail::glob_match(pat, path)) return true;
            auto slash = path.rfind('/');
            auto base = slash == std::string_view::npos ? path : path.substr(slash + 1);
            if (detail::glob_match(pat, base)) return true;
        }
        return false;
    };

    std::istringstream in{std::string(diff)};
    std::string line;
    std::string leading;
    std::string out;
    std::string block;
    std::string path;
    std::string pending_old;
    bool in_block = false;

    auto flush = [&] {
        if (!in_block) return;
        bool keep = true;
        if (!includes.empty() && !matches(path, includes)) keep = false;
        if (matches(path, excludes)) keep = false;
        if (keep) out += block;
        block.clear();
    };

    while (std::getline(in, line)) {
        if (line.starts_with("diff ")) {
            flush();
            in_block = true;
            path = detail::path_from_diff_header(line);
            pending_old.clear();
            block += line;
            block.push_back('\n');
            continue;
        }
        if (!in_block) {
            leading += line;
            leading.push_back('\n');
            continue;
        }
        block += line;
        block.push_back('\n');

        std::string_view lv{line};
        if (lv.starts_with("--- ")) {
            auto p = detail::strip_quotes(lv.substr(4));
            if (p.starts_with("a/")) pending_old = std::string(p.substr(2));
        } else if (lv.starts_with("+++ ")) {
            auto p = detail::strip_quotes(lv.substr(4));
            if (p == "/dev/null") path = pending_old;
            else if (p.starts_with("b/"))
                path = std::string(p.substr(2));
        }
    }
    flush();

    return leading + out;
}
