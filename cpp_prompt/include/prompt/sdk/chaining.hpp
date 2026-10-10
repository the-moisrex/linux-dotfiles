#pragma once

#include "prompt/sdk/prompt_registry.hpp"
#include <array>
#include <span>
#include <string_view>

namespace prompt {

struct prompt_invocation {
    std::string_view name;
    std::array<std::string_view, 16> args{};
    std::size_t args_count = 0;
};

struct chain_result {
    std::array<prompt_invocation, 8> invocations{};
    std::size_t count = 0;
};

chain_result parse_chain(
    std::span<char const* const> argv,
    std::span<prompt_descriptor const> registry) noexcept;

} // namespace prompt