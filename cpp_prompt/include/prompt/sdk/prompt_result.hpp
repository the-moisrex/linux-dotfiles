#pragma once

#include <string>

namespace prompt {

struct prompt_result {
    std::string output;
    int exit_code = 0;
    bool no_clipboard = false;
};

} // namespace prompt