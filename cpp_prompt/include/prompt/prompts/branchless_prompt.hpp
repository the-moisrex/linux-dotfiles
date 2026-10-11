#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"

namespace prompt::prompts {

prompt_result execute_branchless(prompt_context&& ctx) noexcept;
void render_help_branchless(std::ostream& os) noexcept;

} // namespace prompt::prompts
