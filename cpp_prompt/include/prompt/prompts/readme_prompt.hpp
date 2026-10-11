#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"
#include <ostream>

namespace prompt::prompts {

prompt_result execute_readme(prompt_context&& ctx) noexcept;
void render_help_readme(std::ostream& os) noexcept;

} // namespace prompt::prompts
