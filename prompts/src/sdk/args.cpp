#include "args.hpp"
#include "errors.hpp"
#include <cstdlib>
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
            if (digits) head_lines = static_cast<std::size_t>(std::strtoul(std::string(value).c_str(), nullptr, 10));
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

std::optional<std::size_t> head_lines_or_throw(prompt_context const& ctx, bool strict) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines, strict); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }
    return head_lines;
}

std::vector<std::string> positional_args(prompt_context const& ctx) {
    std::vector<std::string> out;
    out.reserve(ctx.args_count);
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        out.emplace_back(ctx.args[i]);
    }
    return out;
}

file_args collect_file_args(prompt_context const& ctx) {
    file_args result;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            result.files.push_back(std::move(file));
        } else {
            result.missing.push_back(ctx.args[i]);
        }
    }
    return result;
}

} // namespace prompt
