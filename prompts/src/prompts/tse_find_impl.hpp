#pragma once

#include "../sdk/errors.hpp"
#include "../sdk/prompt_context.hpp"

namespace prompt::prompts {

// Native port of prompts/tse.find.sh: the chat/agentic command generator, the
// .tse snapshot review and the piped tse.find output review. Produces the same
// stdout, stderr and exit codes as the bash reference's `tse.find`.
void execute_tse_find_impl(prompt_context& ctx);
void render_help_tse_find_impl(std::ostream& os);

} // namespace prompt::prompts
