#pragma once

#include "completion.hpp"
#include "errors.hpp"
#include "prompt_context.hpp"
#include "sink_types.hpp"
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
    // Sinks run at a barrier and receive the chain text accumulated before
    // them. Their common flags live in sdk/sink.hpp; the dispatcher owns them.
    chain_role role = chain_role::normal;
    sink_policy policy = sink_policy::chain;
    // A silent/passthrough sink (`.save`) never writes stdout and does not
    // spend the stdin pipe: the dispatcher must not open a streamed-output
    // separator for it and must keep piped stdin in the accumulated context.
    bool stream_output = true;
    // Optional validation/early-setup hook. The dispatcher runs it after chain
    // and sink parsing but before any prompt executes, so a sink such as
    // `.save` can create and check its destination directory without doing the
    // expensive work first. It throws prompt_error on failure.
    void (*preflight_fn)(prompt_context&) = nullptr;
};

std::span<prompt_descriptor const> get_all_prompts();

prompt_descriptor const* find_prompt(std::string_view name);

// Locale-aware name ordering (LC_COLLATE from the environment), shared by
// list/list-prompts/delegate so the catalog has one order.
bool prompt_descriptor_less(prompt_descriptor const* a, prompt_descriptor const* b) noexcept;

std::vector<std::string_view> list_prompt_names();

// Every prompt registers itself: each src/prompts/<name>.cpp ends with one
// call like
//
//     bool const registered = prompt::register_prompt(
//         "api", "Review this API design.", &execute_api, &render_help_api);
//
// which runs at static-init time, before main. Nothing centralises the list,
// so adding a prompt means editing exactly one file. Returns false when the
// registry is full (a dropped registration would silently shadow a prompt).
bool register_prompt(std::string_view name, std::string_view help_summary, void (*execute_fn)(prompt_context&),
                     void (*render_help_fn)(std::ostream&), completion_fn complete_fn = nullptr,
                     chain_role role = chain_role::normal, sink_policy policy = sink_policy::chain,
                     bool stream_output = true, void (*preflight_fn)(prompt_context&) = nullptr) noexcept;

// Register a sink: the same registry entry with role=sink. Sink flags
// (--on-error/--with-context/--show-context/--head) are handled by the
// dispatcher through sdk/sink.hpp, so adding a sink only needs its own
// executor, help renderer, and policy. `stream_output=false` marks a silent
// passthrough sink such as `.save`, whose empty stdout must not open an output
// separator and which must leave piped stdin for later segments.
// `preflight_fn` is run by the dispatcher before any prompt executes.
bool register_sink(std::string_view name, std::string_view help_summary, void (*execute_fn)(prompt_context&),
                   void (*render_help_fn)(std::ostream&), completion_fn complete_fn = nullptr,
                   sink_policy policy = sink_policy::chain, bool stream_output = true,
                   void (*preflight_fn)(prompt_context&) = nullptr) noexcept;

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
