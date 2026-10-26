#pragma once

#include "../core/strings.hpp"
#include "facts.hpp"
#include <string>
#include <string_view>

namespace prompt {

// The dispatcher's single accumulator. `append` is the one text-merge rule
// (`text::append_block`): trim the block on both sides, skip it when empty, and
// separate consecutive blocks with one blank line. `known` holds the artifacts
// of completed segments (last writer wins: the nearest successful segment).
struct chain_state {
    std::string text;
    facts known;
    bool redacted = false;
    bool incomplete = false;

    void append(std::string_view block, bool block_redacted) {
        if (!text::append_block(text, block)) return;
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
