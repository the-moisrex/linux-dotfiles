#include "prompt/prompts/prompts.hpp"
#include "prompt/prompts/tse_find_impl.hpp"

namespace prompt::prompts {

prompt_output execute_tse_find(prompt_context&& ctx) { return execute_tse_find_impl(std::move(ctx)); }

void render_help_tse_find(std::ostream& os) { render_help_tse_find_impl(os); }

} // namespace prompt::prompts
