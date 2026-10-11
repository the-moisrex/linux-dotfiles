#include "prompt/prompts/tse_find_prompt.hpp"
#include "prompt/prompts/tse_find_impl.hpp"

namespace prompt::prompts {

prompt_result execute_tse_find(prompt_context&& ctx) noexcept { return execute_tse_find_impl(std::move(ctx)); }

void render_help_tse_find(std::ostream& os) noexcept { render_help_tse_find_impl(os); }

} // namespace prompt::prompts
