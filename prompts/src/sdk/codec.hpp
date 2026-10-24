#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace prompt::codec {

// The two primitives the length-prefixed wire blobs are built from: the fact
// store's payload and the fork codec's segment_outcome share them, so the
// encoding lives in exactly one place.

inline void append_u32(std::string& out, std::size_t value) {
    auto const narrow = static_cast<std::uint32_t>(value);
    out.append(reinterpret_cast<char const*>(&narrow), sizeof narrow);
}

// Read a u32 at `offset`, advancing it on success. A blob that is too short
// leaves `offset` alone, so a truncated payload stops the caller cleanly.
inline bool read_u32(std::string_view blob, std::size_t& offset, std::uint32_t& value) {
    if (offset > blob.size() || blob.size() - offset < sizeof value) return false;
    std::memcpy(&value, blob.data() + offset, sizeof value);
    offset += sizeof value;
    return true;
}

} // namespace prompt::codec
