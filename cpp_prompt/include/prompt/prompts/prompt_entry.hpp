#pragma once

#include "prompt/prompts/prompts.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <functional>
#include <string>

namespace prompt {

void init_prompt_registry();

// Batch registrars (see src/prompts/register_batches.cpp).
void register_batches();

} // namespace prompt