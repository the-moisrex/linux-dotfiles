#pragma once

#include "completion.hpp"
#include "errors.hpp"
#include "prompt_context.hpp"
#include "sink_types.hpp"
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
    // Optional positional/value completion (`--complete`), in addition to the
    // flags the dispatcher derives from render_help_fn. nullptr = flags only.
    completion_fn complete_fn;
    // The bash dispatcher's collect_prompts pairs each name with its script
    // path, and list/list-prompts sort whole `name<TAB>file` lines under
    // LC_COLLATE — the file part influences the collation result. This build
    // keeps the same key (prompt files lived in <repo>/prompts/) purely so
    // the ordering matches the bash dispatcher; it never opens the file.
    std::string_view sort_file;
    // Sinks run at a barrier and receive the chain text accumulated before
    // them. Their common flags live in sdk/sink.hpp; the dispatcher owns them.
    chain_role role = chain_role::normal;
    sink_policy policy = sink_policy::chain;
    // A silent/passthrough sink (`.save`) never writes stdout and does not
    // spend the stdin pipe: the dispatcher must not open a streamed-output
    // separator for it and must keep piped stdin in the accumulated context.
    bool stream_output = true;
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
                     void (*render_help_fn)(std::ostream&), completion_fn complete_fn = nullptr,
                     chain_role role = chain_role::normal, sink_policy policy = sink_policy::chain,
                     bool stream_output = true) noexcept;

// Register a sink: the same registry entry with role=sink. Sink flags
// (--on-error/--with-context/--show-context/--head) are handled by the
// dispatcher through sdk/sink.hpp, so adding a sink only needs its own
// executor, help renderer, and policy. `stream_output=false` marks a silent
// passthrough sink such as `.save`, whose empty stdout must not open an output
// separator and which must leave piped stdin for later segments.
bool register_sink(std::string_view name, std::string_view help_summary, void (*execute_fn)(prompt_context&),
                   void (*render_help_fn)(std::ostream&), completion_fn complete_fn = nullptr,
                   sink_policy policy = sink_policy::chain, bool stream_output = true) noexcept;

// Fills in the sort_file keys; callers must do this after main starts (it
// needs the filesystem). Registration itself happens before main.
void init_prompt_registry();

// Captures a prompt's own `render_help_*` output into a string, for prompts
// that append it to a failure message (cli, man, tse.analyze, tse.intraday) instead of
// printing it directly (`help_text(render_help_cli)` instead of the
// ostringstream dance).
template <typename RenderFn>
std::string help_text(RenderFn render) {
    std::ostringstream os;
    render(os);
    return os.str();
}

} // namespace prompt
