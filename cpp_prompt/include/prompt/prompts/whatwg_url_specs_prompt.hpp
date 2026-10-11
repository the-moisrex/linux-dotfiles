#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"

namespace prompt::prompts {

prompt_result execute_whatwg_url_specs(prompt_context&& ctx) noexcept;
void render_help_whatwg_url_specs(std::ostream& os) noexcept;

} // namespace prompt::prompts
