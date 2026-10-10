#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"

namespace prompt::legacy {

// Execute the legacy prompt script named by ctx.name (bash/txt/md file
// from the search directories), forwarding args and stdin and honoring the
// script's no-clipboard opt-out.
prompt_result execute_legacy_prompt(prompt_context&& ctx) noexcept;

// Register every prompt file found in the search directories that is not
// already registered (native prompts win; first search dir wins), with its
// statically extracted help. Called from init_prompt_registry so every
// script is addressable by name — bash dispatcher parity.
void register_legacy_prompts() noexcept;

} // namespace prompt::legacy
