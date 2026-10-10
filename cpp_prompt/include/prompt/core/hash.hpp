#pragma once

#include <cstdint>
#include <string_view>

namespace prompt::hash {

constexpr std::uint32_t fnv1a_32(std::string_view data) noexcept {
    constexpr std::uint32_t FNV1A_32_INIT = 0x811C9DC5U;
    constexpr std::uint32_t FNV1A_32_PRIME = 0x01000193U;
    std::uint32_t hash = FNV1A_32_INIT;
    for (char c : data) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= FNV1A_32_PRIME;
    }
    return hash;
}

constexpr std::uint32_t fnv1a_32_ci(std::string_view data) noexcept {
    constexpr std::uint32_t FNV1A_32_INIT = 0x811C9DC5U;
    constexpr std::uint32_t FNV1A_32_PRIME = 0x01000193U;
    std::uint32_t hash = FNV1A_32_INIT;
    for (char c : data) {
        char lower = (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
        hash ^= static_cast<std::uint8_t>(lower);
        hash *= FNV1A_32_PRIME;
    }
    return hash;
}

constexpr std::uint32_t combine_hashes(std::uint32_t h1, std::uint32_t h2) noexcept {
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

} // namespace prompt::hash