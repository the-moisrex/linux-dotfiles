#pragma once

#include "prompt/core/process.hpp"
#include <string>
#include <string_view>

namespace prompt::legacy {

// Run a legacy bash prompt script (prompts/<name>.sh etc.) directly,
// bypassing the prompt dispatcher to avoid recursion. Mirrors the bash
// dispatcher's _find_prompt_file search order and handling.
process::command_result run_prompt_script(
    std::string_view name,
    std::string_view stdin_data,
    std::span<char const* const> args) noexcept;

} // namespace prompt::legacy
