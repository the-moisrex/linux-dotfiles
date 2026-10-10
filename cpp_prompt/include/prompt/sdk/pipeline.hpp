#pragma once

#include <string>
#include <string_view>

namespace prompt {

struct pipeline {
    std::string accumulated_output;
    std::string clipboard_output;
    bool first_prompt = true;
    bool first_clipboard = true;
    
    void add_output(std::string_view output, bool no_clipboard) noexcept {
        if (output.empty()) return;
        if (!first_prompt) {
            accumulated_output += '\n';
        }
        accumulated_output += output;
        first_prompt = false;
        
        if (!no_clipboard) {
            if (!first_clipboard) {
                clipboard_output += '\n';
            }
            clipboard_output += output;
            first_clipboard = false;
        }
    }
    
    [[nodiscard]] std::string finalize() const noexcept {
        return accumulated_output;
    }
    
    [[nodiscard]] std::string for_clipboard() const noexcept {
        return clipboard_output;
    }
};

} // namespace prompt
