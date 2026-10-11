#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"
#include <ostream>

namespace prompt::prompts {

prompt_result execute_optimize_prompt(prompt_context&& ctx) noexcept;
void render_help_optimize_prompt(std::ostream& os) noexcept;

} // namespace prompt::prompts
