#pragma once

#include "../sdk/prompt_registry.hpp"
#include "prompts.hpp"
#include <functional>
#include <string>

namespace prompt {

void init_prompt_registry();

// Batch registrars (see src/prompts/register_batches.cpp).
void register_batches();

} // namespace prompt