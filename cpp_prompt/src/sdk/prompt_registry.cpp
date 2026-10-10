#include "prompt/sdk/prompt_registry.hpp"
#include <algorithm>
#include <array>
#include <clocale>
#include <cstring>
#include <string>
#include <vector>

namespace prompt {

namespace {
// 21 native prompts + up to ~76 legacy prompt files.
std::array<prompt_descriptor, 128> prompts_storage{};
std::size_t prompts_count = 0;
} // namespace

bool prompt_descriptor_less(prompt_descriptor const* a, prompt_descriptor const* b) noexcept {
    // The bash dispatcher pipes whole `name<TAB>file` lines through `sort`
    // under the caller's LC_COLLATE, and the file part can influence the
    // collation result — so the key must include it verbatim.
    static bool const locale_set = std::setlocale(LC_COLLATE, "") != nullptr;
    (void)locale_set;
    std::string const ka = std::string(a->name) + '\t' + std::string(a->sort_file);
    std::string const kb = std::string(b->name) + '\t' + std::string(b->sort_file);
    int const c = std::strcoll(ka.c_str(), kb.c_str());
    if (c != 0) return c < 0;
    return a->name < b->name; // collation-equal keys: deterministic tiebreak
}

void set_sort_file(std::string_view name, std::string_view file) noexcept {
    for (std::size_t i = 0; i < prompts_count; ++i) {
        if (prompts_storage[i].name == name) {
            // First search dir wins (collect_prompts' `seen` semantics).
            if (prompts_storage[i].sort_file.empty()) {
                prompts_storage[i].sort_file = file;
            }
            return;
        }
    }
}

void set_help_texts(std::string_view name, std::string_view summary, std::string_view full) noexcept {
    for (std::size_t i = 0; i < prompts_count; ++i) {
        if (prompts_storage[i].name == name) {
            prompts_storage[i].help_summary = summary;
            prompts_storage[i].help_full = full;
            return;
        }
    }
}

std::span<prompt_descriptor const> get_all_prompts() noexcept { return {prompts_storage.data(), prompts_count}; }

prompt_descriptor const* find_prompt(std::string_view name) noexcept {
    for (std::size_t i = 0; i < prompts_count; ++i) {
        if (prompts_storage[i].name == name) {
            return &prompts_storage[i];
        }
    }
    return nullptr;
}

std::vector<std::string_view> list_prompt_names() noexcept {
    std::vector<prompt_descriptor const*> sorted;
    sorted.reserve(prompts_count);
    for (std::size_t i = 0; i < prompts_count; ++i) {
        sorted.push_back(&prompts_storage[i]);
    }
    std::sort(sorted.begin(), sorted.end(), prompt_descriptor_less);
    std::vector<std::string_view> names;
    names.reserve(sorted.size());
    for (auto const* desc : sorted) names.push_back(desc->name);
    return names;
}

void register_prompt(prompt_descriptor const& desc) noexcept {
    if (prompts_count < prompts_storage.size()) {
        prompts_storage[prompts_count++] = desc;
    }
}

} // namespace prompt