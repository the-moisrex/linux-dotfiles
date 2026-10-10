#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace prompt {

struct prompt_descriptor {
    std::string_view name;
    std::string_view help_summary;
    std::string_view help_full;
    prompt_result (*execute_fn)(prompt_context&&) noexcept;
    void (*render_help_fn)(std::ostream&) noexcept;
};

std::span<prompt_descriptor const> get_all_prompts() noexcept;

prompt_descriptor const* find_prompt(std::string_view name) noexcept;

std::vector<std::string_view> list_prompt_names() noexcept;

void register_prompt(prompt_descriptor const& desc) noexcept;

void init_prompt_registry() noexcept;

} // namespace prompt