#include "prompt_registry.hpp"
#include <algorithm>
#include <array>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace prompt {

namespace {
// Every prompt is native. The catalog is fixed at build time; register_prompt
// aborts if it ever outgrows this, because a dropped registration would
// silently remove a prompt from the command.
std::array<prompt_descriptor, prompt_registry_capacity> prompts_storage{};
std::size_t prompts_count = 0;
} // namespace

bool prompt_descriptor_less(prompt_descriptor const* a, prompt_descriptor const* b) noexcept {
    // Locale-aware name ordering (LC_COLLATE from the environment) so
    // `list`, `list-prompts` and `delegate` agree on one catalog order.
    static bool const locale_set = std::setlocale(LC_COLLATE, "") != nullptr;
    (void)locale_set;
    std::string const ka(a->name);
    std::string const kb(b->name);
    int const c = std::strcoll(ka.c_str(), kb.c_str());
    if (c != 0) return c < 0;
    return a->name < b->name; // collation-equal names: deterministic tiebreak
}

std::span<prompt_descriptor const> get_all_prompts() { return {prompts_storage.data(), prompts_count}; }

prompt_descriptor const* find_prompt(std::string_view name) {
    for (std::size_t i = 0; i < prompts_count; ++i) {
        if (prompts_storage[i].name == name) {
            return &prompts_storage[i];
        }
    }
    return nullptr;
}

std::vector<std::string_view> list_prompt_names() {
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

bool register_prompt(std::string_view name, std::string_view help_summary, exec_fn execute_fn,
                     void (*render_help_fn)(std::ostream&), completion_fn complete_fn, chain_role role,
                     bool stream_output, preflight_fn preflight, declare_fn declare, bool takes_body,
                     bool record) noexcept {
    if (prompts_count >= prompts_storage.size()) {
        // Static-init registration cannot report an error to anyone: aborts with
        // a message a developer can act on.
        std::fputs("prompt: prompt registry is full; raise prompt_registry_capacity\n", stderr);
        std::abort();
    }
    prompts_storage[prompts_count++] = prompt_descriptor{
        .name = name,
        .help_summary = help_summary,
        .execute_fn = execute_fn,
        .render_help_fn = render_help_fn,
        .complete_fn = complete_fn,
        .role = role,
        .stream_output = stream_output,
        .preflight = preflight,
        .declare = declare,
        .takes_body = takes_body,
        .record = record,
    };
    return true;
}

bool register_sink(std::string_view name, std::string_view help_summary, exec_fn execute_fn,
                   void (*render_help_fn)(std::ostream&), completion_fn complete_fn, bool stream_output,
                   preflight_fn preflight, declare_fn declare, bool takes_body, bool record) noexcept {
    return register_prompt(name, help_summary, execute_fn, render_help_fn, complete_fn, chain_role::sink, stream_output,
                           preflight, declare, takes_body, record);
}

} // namespace prompt