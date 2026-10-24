#pragma once

#include "../sdk/errors.hpp"
#include "../sdk/stage.hpp"

namespace prompt::prompts {

// The chat/agentic command generator, the .tse snapshot review and the piped
// tse.find output review shown by `prompt tse.find`.
void execute_tse_find_impl(view const& v, result& r);
void render_help_tse_find_impl(std::ostream& os);

} // namespace prompt::prompts
