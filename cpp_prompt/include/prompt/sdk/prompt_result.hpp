#pragma once

#include <string>

namespace prompt {

struct prompt_result {
    // Prompt text for stdout (always printed, even on failure — the bash
    // dispatcher accumulates a script's stdout regardless of its status).
    std::string output;
    int exit_code = 0;
    bool no_clipboard = false;
    // Diagnostics for stderr (bash scripts print these with `>&2`), written
    // verbatim and never copied to the clipboard.
    std::string error{};
    // True when this prompt read the piped stdin. The dispatcher shares one
    // pipe across a chain: once a prompt has consumed it, later prompts see
    // an empty (but still piped) stdin, exactly like bash.
    bool stdin_consumed = false;
};

} // namespace prompt