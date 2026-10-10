#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"

namespace prompt::prompts {

prompt_result execute_auto(prompt_context&& ctx) noexcept;
void render_help_auto(std::ostream& os) noexcept;

} // namespace prompt::prompts