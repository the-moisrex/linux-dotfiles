#pragma once

#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_result.hpp"
#include <filesystem>
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
    // The bash dispatcher's collect_prompts pairs each name with its script
    // path, and list/list-prompts sort whole `name<TAB>file` lines under
    // LC_COLLATE — the file part influences the collation result. This build
    // keeps the same key (prompt files lived in <repo>/prompts/) purely so
    // the ordering matches the bash dispatcher; it never opens the file.
    std::string_view sort_file{};
};

std::span<prompt_descriptor const> get_all_prompts() noexcept;

// Set the sort_file of every registered prompt to <dir>/<name>.sh
// (bash-dispatcher collation parity).
void assign_sort_files(std::filesystem::path const& dir) noexcept;

prompt_descriptor const* find_prompt(std::string_view name) noexcept;

// Locale-aware ordering over `name<TAB>file` sort keys (LC_COLLATE from
// the environment), matching the `sort` the bash dispatcher applies to
// collect_prompts output for list/list-prompts.
bool prompt_descriptor_less(prompt_descriptor const* a, prompt_descriptor const* b) noexcept;

std::vector<std::string_view> list_prompt_names() noexcept;

void register_prompt(prompt_descriptor const& desc) noexcept;

void init_prompt_registry() noexcept;

} // namespace prompt