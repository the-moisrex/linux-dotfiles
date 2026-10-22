#pragma once

#include "../sdk/errors.hpp"
#include "../sdk/prompt_context.hpp"

namespace prompt::prompts {

// The chat/agentic command generator, the .tse snapshot review and the piped
// tse.find output review shown by `prompt tse.find`.
void execute_tse_find_impl(prompt_context& ctx);
void render_help_tse_find_impl(std::ostream& os);

} // namespace prompt::prompts
