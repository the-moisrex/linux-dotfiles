#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"

namespace prompt::prompts {

prompt_result execute_ask_for_help(prompt_context&& ctx) noexcept;
void render_help_ask_for_help(std::ostream& os) noexcept;

} // namespace prompt::prompts
