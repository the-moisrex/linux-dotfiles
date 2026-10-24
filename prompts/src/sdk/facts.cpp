#include "facts.hpp"
#include "codec.hpp"
#include <algorithm>

namespace prompt {

void facts::set(std::string_view key, std::string_view value) {
    auto it = std::ranges::find_if(entries_, [key](entry const& e) { return e.key == key; });
    if (it == entries_.end()) {
        entries_.push_back(entry{.key = std::string(key), .value = std::string(value)});
    } else {
        it->value.assign(value);
    }
}

std::optional<std::string_view> facts::get(std::string_view key) const {
    auto it = std::ranges::find_if(entries_, [key](entry const& e) { return e.key == key; });
    if (it == entries_.end()) return std::nullopt;
    return it->value;
}

void facts::merge(facts const& other, bool overwrite) {
    for (auto const& item : other.entries_) {
        if (overwrite) {
            set(item.key, item.value);
            continue;
        }
        if (!get(item.key)) set(item.key, item.value);
    }
}

std::string facts::encode() const {
    std::string out;
    out.reserve(entries_.size() * 32);
    codec::append_u32(out, entries_.size());
    for (auto const& item : entries_) {
        codec::append_u32(out, item.key.size());
        out += item.key;
        codec::append_u32(out, item.value.size());
        out += item.value;
    }
    return out;
}

facts facts::decode(std::string_view blob) {
    facts out;
    std::size_t offset = 0;
    std::uint32_t count = 0;
    if (!codec::read_u32(blob, offset, count)) return out;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t key_size = 0;
        if (!codec::read_u32(blob, offset, key_size) || blob.size() - offset < key_size) return out;
        std::string_view const key = blob.substr(offset, key_size);
        offset += key_size;

        std::uint32_t value_size = 0;
        if (!codec::read_u32(blob, offset, value_size) || blob.size() - offset < value_size) return out;
        std::string_view const value = blob.substr(offset, value_size);
        offset += value_size;

        out.set(key, value);
    }
    return out;
}

} // namespace prompt
