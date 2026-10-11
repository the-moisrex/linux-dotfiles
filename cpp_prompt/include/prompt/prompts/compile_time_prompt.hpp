#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"

namespace prompt::prompts {

prompt_result execute_compile_time(prompt_context&& ctx) noexcept;
void render_help_compile_time(std::ostream& os) noexcept;

} // namespace prompt::prompts
