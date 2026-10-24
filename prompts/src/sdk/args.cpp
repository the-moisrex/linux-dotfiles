#include "args.hpp"
#include "errors.hpp"
#include <algorithm>
#include <cstdlib>
#include <format>
#include <limits>

namespace prompt {

std::string parse_head_option(std::span<std::string_view const> args, std::optional<std::size_t>& head_lines,
                              bool strict) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] != "--head") continue;
        if (i + 1 >= args.size()) return "Missing value for --head\n";
        std::string_view value = args[i + 1];
        if (!strict) {
            // Scripts with their own parser accept any value verbatim (they
            // never read it); bash's `shift 2` keeps the last --head.
            bool digits = !value.empty() && value.find_first_not_of("0123456789") == std::string_view::npos;
            if (digits) head_lines = std::strtoul(std::string(value).c_str(), nullptr, 10);
            ++i;
            continue;
        }
        if (value.empty() || value.find_first_not_of("0123456789") != std::string_view::npos) {
            return "--head requires a non-negative integer\n";
        }
        std::string copy(value);
        char const* begin = copy.c_str();
        char* end = nullptr;
        unsigned long long parsed = std::strtoull(begin, &end, 10);
        if (end != begin + copy.size()) {
            return "--head requires a non-negative integer\n";
        }
        head_lines = parsed > std::numeric_limits<std::size_t>::max() ? std::numeric_limits<std::size_t>::max()
                                                                      : static_cast<std::size_t>(parsed);
        ++i;
    }
    return {};
}

std::optional<std::size_t> head_lines_or_throw(view const& in, bool strict) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option(in.args, head_lines, strict); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }
    return head_lines;
}

bool is_number_flag(std::string_view arg) {
    return std::ranges::any_of(kNumberFlags, [arg](std::string_view flag) { return arg == flag; });
}

file_options file_options_or_throw(view const& in, bool strict) {
    file_options opts;
    if (auto msg = parse_head_option(in.args, opts.head_lines, strict); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }
    for (std::string_view arg : in.args) {
        if (is_number_flag(arg)) {
            opts.numbered = true;
            break;
        }
    }
    return opts;
}

std::string file_usage_help(std::string_view name, std::string_view tail, std::string_view pipe,
                            std::string_view prefix) {
    std::string flags = std::format("{}[--head N] [--num]", prefix.empty() ? "" : std::format("{} ", prefix));
    std::string args = tail.empty() ? flags : std::format("{} {}", flags, tail);
    std::string out = std::format("Usage: prompt {0} {1}\n", name, args);
    if (!pipe.empty()) {
        out += std::format("       {0} | prompt {1} {2}\n", pipe, name, args);
    }
    return out;
}

std::string file_options_help(std::string_view what, bool show_help) {
    std::string help = std::format("  --head N   Keep only the first N lines of {}\n", what);
    help += "  --num      Prefix each embedded line with its line number (alias: --numbered)\n";
    if (show_help) {
        help += "  -h, --help Show this help message\n";
    }
    return help;
}

std::vector<std::string> positional_args(std::span<std::string_view const> args) {
    std::vector<std::string> out;
    out.reserve(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head") {
            ++i;
            continue;
        }
        if (is_number_flag(args[i])) continue;
        out.emplace_back(args[i]);
    }
    return out;
}

std::vector<std::string> positional_args(view const& in) { return positional_args(in.args); }

file_args collect_file_args(std::span<std::string_view const> args) {
    file_args result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head") {
            ++i;
            continue;
        }
        if (is_number_flag(args[i])) continue;
        std::filesystem::path file(args[i]);
        if (std::filesystem::is_regular_file(file)) {
            result.files.push_back(std::move(file));
        } else {
            result.missing.push_back(args[i]);
        }
    }
    return result;
}

file_args collect_file_args(view const& in) { return collect_file_args(in.args); }

} // namespace prompt
