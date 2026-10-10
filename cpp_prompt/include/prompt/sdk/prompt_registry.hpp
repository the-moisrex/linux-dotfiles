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
    // Non-empty for dynamically registered legacy prompts: the path of the
    // script file (bash/txt/md) backing this prompt. Empty for native ones.
    std::string_view script{};

    // The file path the bash dispatcher's collect_prompts pairs with this
    // name. list/list-prompts sort whole `name<TAB>file` lines under
    // LC_COLLATE, and the file part influences the collation result — so
    // byte-parity with bash needs the exact path here. Set during legacy
    // registration (including for native-shadowed names).
    std::string_view sort_file{};
};

std::span<prompt_descriptor const> get_all_prompts() noexcept;

prompt_descriptor const* find_prompt(std::string_view name) noexcept;

// Locale-aware ordering over `name<TAB>file` sort keys (LC_COLLATE from
// the environment), matching the `sort` the bash dispatcher applies to
// collect_prompts output for list/list-prompts.
bool prompt_descriptor_less(prompt_descriptor const* a, prompt_descriptor const* b) noexcept;

// Attach the collect_prompts file path to an already-registered name
// (native prompts are registered before their backing files are scanned).
void set_sort_file(std::string_view name, std::string_view file) noexcept;

// Replace an already-registered prompt's help text (used when a script
// shadows a native prompt: bash's list/--help come from the script).
void set_help_texts(std::string_view name, std::string_view summary, std::string_view full) noexcept;

std::vector<std::string_view> list_prompt_names() noexcept;

void register_prompt(prompt_descriptor const& desc) noexcept;

void init_prompt_registry() noexcept;

} // namespace prompt