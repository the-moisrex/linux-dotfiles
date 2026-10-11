#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"
#include <ostream>

namespace prompt::prompts {

prompt_result execute_de_export(prompt_context&& ctx) noexcept;
void render_help_de_export(std::ostream& os) noexcept;

} // namespace prompt::prompts
