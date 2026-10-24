#pragma once

#include "facts.hpp"
#include <string>
#include <string_view>

namespace prompt {

// The dispatcher's single accumulator. `append` is the only text-merging rule:
// trim the block, skip it when empty, and separate consecutive blocks with one
// blank line. `known` holds the artifacts of completed segments (last writer
// wins: the nearest successful segment).
struct chain_state {
    std::string text;
    facts known;
    bool redacted = false;
    bool incomplete = false;

    void append(std::string_view block, bool block_redacted) {
        std::string_view trimmed = block;
        while (!trimmed.empty() && trimmed.front() == '\n') trimmed.remove_prefix(1);
        while (!trimmed.empty() && trimmed.back() == '\n') trimmed.remove_suffix(1);
        if (trimmed.empty()) return;
        if (!text.empty()) {
            while (!text.empty() && text.back() == '\n') text.pop_back();
            text += "\n\n";
        }
        text.append(trimmed);
        text += '\n';
        redacted = redacted || block_redacted;
    }

    void clear() {
        text.clear();
        known.clear();
        redacted = false;
        incomplete = false;
    }
};

} // namespace prompt
