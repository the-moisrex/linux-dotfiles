#include "prompt/sdk/args.hpp"
#include <cstdlib>
#include <limits>

namespace prompt {

std::string parse_head_option(std::span<std::string_view const> args, std::size_t& head_lines, bool strict) noexcept {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] != "--head") continue;
        if (i + 1 >= args.size()) return "Missing value for --head\n";
        std::string_view value = args[i + 1];
        if (!strict) {
            bool digits = !value.empty() && value.find_first_not_of("0123456789") == std::string_view::npos;
            if (!digits) return {};
            head_lines = static_cast<std::size_t>(std::strtoul(std::string(value).c_str(), nullptr, 10));
            return {};
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
        return {};
    }
    return {};
}

} // namespace prompt
