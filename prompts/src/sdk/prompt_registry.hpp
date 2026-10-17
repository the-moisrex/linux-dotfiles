#pragma once

#include "errors.hpp"
#include "prompt_context.hpp"
#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

struct prompt_descriptor {
    std::string_view name;
    std::string_view help_summary;
    void (*execute_fn)(prompt_context&);
    void (*render_help_fn)(std::ostream&);
    // The bash dispatcher's collect_prompts pairs each name with its script
    // path, and list/list-prompts sort whole `name<TAB>file` lines under
    // LC_COLLATE — the file part influences the collation result. This build
    // keeps the same key (prompt files lived in <repo>/prompts/) purely so
    // the ordering matches the bash dispatcher; it never opens the file.
    std::string_view sort_file;
};

std::span<prompt_descriptor const> get_all_prompts();

// Set the sort_file of every registered prompt to <dir>/<name>.sh
// (bash-dispatcher collation parity).
void assign_sort_files(std::filesystem::path const& dir);

prompt_descriptor const* find_prompt(std::string_view name);

// Locale-aware ordering over `name<TAB>file` sort keys (LC_COLLATE from
// the environment), matching the `sort` the bash dispatcher applies to
// collect_prompts output for list/list-prompts.
bool prompt_descriptor_less(prompt_descriptor const* a, prompt_descriptor const* b) noexcept;

std::vector<std::string_view> list_prompt_names();

// Every prompt registers itself: each src/prompts/<name>.cpp ends with one
// call like
//
//     bool const registered = prompt::register_prompt(
//         "api", "Review this API design.", &execute_api, &render_help_api);
//
// which runs at static-init time, before main. Nothing centralises the list,
// so adding a prompt means editing exactly one file. `sort_file` is filled in
// later by assign_sort_files(). Returns false when the registry is full (a
// dropped registration would silently shadow a prompt).
bool register_prompt(std::string_view name, std::string_view help_summary, void (*execute_fn)(prompt_context&),
                     void (*render_help_fn)(std::ostream&)) noexcept;

// Fills in the sort_file keys; callers must do this after main starts (it
// needs the filesystem). Registration itself happens before main.
void init_prompt_registry();

// Captures a prompt's own `render_help_*` output into a string, for prompts
// that append it to a failure message (cli, man, stock, intraday) instead of
// printing it directly (`help_text(render_help_cli)` instead of the
// ostringstream dance).
template <typename RenderFn>
std::string help_text(RenderFn render) {
    std::ostringstream os;
    render(os);
    return os.str();
}

} // namespace prompt
