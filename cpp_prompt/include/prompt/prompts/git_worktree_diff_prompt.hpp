#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"

namespace prompt::prompts {

prompt_result execute_git_worktree_diff(prompt_context&& ctx) noexcept;
void render_help_git_worktree_diff(std::ostream& os) noexcept;

} // namespace prompt::prompts
