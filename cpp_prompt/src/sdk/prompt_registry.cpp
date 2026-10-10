#include "prompt/sdk/prompt_registry.hpp"
#include <array>
#include <vector>

namespace prompt {

namespace {
    std::array<prompt_descriptor, 64> prompts_storage{};
    std::size_t prompts_count = 0;
}

std::span<prompt_descriptor const> get_all_prompts() noexcept {
    return {prompts_storage.data(), prompts_count};
}

prompt_descriptor const* find_prompt(std::string_view name) noexcept {
    for (std::size_t i = 0; i < prompts_count; ++i) {
        if (prompts_storage[i].name == name) {
            return &prompts_storage[i];
        }
    }
    return nullptr;
}

std::vector<std::string_view> list_prompt_names() noexcept {
    std::vector<std::string_view> names;
    names.reserve(prompts_count);
    for (std::size_t i = 0; i < prompts_count; ++i) {
        names.push_back(prompts_storage[i].name);
    }
    return names;
}

void register_prompt(prompt_descriptor const& desc) noexcept {
    if (prompts_count < prompts_storage.size()) {
        prompts_storage[prompts_count++] = desc;
    }
}

} // namespace prompt