#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"

namespace prompt::prompts {

// Native port of prompts/tse.find.sh: the chat/agentic command generator, the
// .tse snapshot review and the piped tse.find output review. Produces the same
// stdout, stderr and exit codes as `bin/prompt tse.find`.
prompt_result execute_tse_find_impl(prompt_context&& ctx) noexcept;
void render_help_tse_find_impl(std::ostream& os) noexcept;

} // namespace prompt::prompts
