#pragma once

#include <string>
#include <string_view>

namespace prompt {

struct pipeline {
    std::string accumulated_output;
    std::string clipboard_output;
    bool first_prompt = true;
    bool first_clipboard = true;

    // Bash captures each prompt's output in $(...) — which strips ALL
    // trailing newlines — then prints it with printf '%s\n'. So every
    // segment is trimmed and re-terminated with exactly one newline, and
    // all-newline output produces nothing at all. The dispatcher still
    // prints its separator `echo` between consecutive prompts even when a
    // prompt produced no output.
    void add_output(std::string_view output, bool no_clipboard) noexcept {
        std::string_view trimmed = output;
        while (!trimmed.empty() && trimmed.back() == '\n') trimmed.remove_suffix(1);

        if (!first_prompt) accumulated_output += '\n'; // blank line between chain segments
        first_prompt = false;

        if (trimmed.empty()) return;
        accumulated_output += trimmed;
        accumulated_output += '\n';

        if (!no_clipboard) {
            if (!first_clipboard) clipboard_output += '\n';
            clipboard_output += trimmed;
            clipboard_output += '\n';
            first_clipboard = false;
        }
    }

    [[nodiscard]] std::string finalize() const noexcept { return accumulated_output; }

    [[nodiscard]] std::string for_clipboard() const noexcept { return clipboard_output; }
};

} // namespace prompt
