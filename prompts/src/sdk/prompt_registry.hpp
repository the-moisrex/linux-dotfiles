#pragma once

#include "completion.hpp"
#include "errors.hpp"
#include "sink_types.hpp"
#include "stage.hpp"
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// One execute signature for every segment, sinks included: a sink is a role in
// the descriptor, not a different function type. The dispatcher owns the
// barrier, the shared flags and the scheduling.
using exec_fn = void (*)(view const&, result&);
using preflight_fn = void (*)(view const&);
using declare_fn = void (*)(declaration&);

struct prompt_descriptor {
    std::string_view name;
    std::string_view help_summary;
    exec_fn execute_fn;
    void (*render_help_fn)(std::ostream&);
    // Optional positional/value completion (`--complete`), in addition to the
    // flags the dispatcher derives from render_help_fn. nullptr = flags only.
    completion_fn complete_fn;
    // Sinks run at a barrier and receive the chain text accumulated before
    // them. Their common flags live in sdk/sink.hpp; the dispatcher owns them.
    chain_role role = chain_role::normal;
    // A silent/passthrough sink (`.save`) never writes stdout and does not
    // spend the stdin pipe: the dispatcher must not open a streamed-output
    // separator for it and must keep piped stdin in the accumulated context.
    bool stream_output = true;
    // Optional validation/early-setup hook. The dispatcher runs it after chain
    // and sink parsing but before any prompt executes, so a sink such as
    // `.save` can create and check its destination directory without doing the
    // expensive work first. It throws prompt_error on failure.
    preflight_fn preflight = nullptr;
    // Optional cheap pre-pass. It runs in the dispatcher before a batch forks,
    // so parallel siblings can agree on argv-derived identity (one tse.resolve
    // per chain, for example). It must not print or touch the network.
    declare_fn declare = nullptr;
};

std::span<prompt_descriptor const> get_all_prompts();

prompt_descriptor const* find_prompt(std::string_view name);

// Locale-aware name ordering (LC_COLLATE from the environment), shared by
// list/list-prompts/delegate so the catalog has one order.
bool prompt_descriptor_less(prompt_descriptor const* a, prompt_descriptor const* b) noexcept;

std::vector<std::string_view> list_prompt_names();

// The fixed registry capacity. A build that registers more prompts than this
// fails loudly (see register_prompt) instead of silently dropping one.
inline constexpr std::size_t prompt_registry_capacity = 128;

// Every prompt registers itself: each src/prompts/<name>.cpp ends with one
// call like
//
//     bool const registered = prompt::register_prompt(
//         "api", "Review this API design.", &execute_api, &render_help_api);
//
// which runs at static-init time, before main. Nothing centralises the list,
// so adding a prompt means editing exactly one file. A registration only fails
// when the catalog outgrows prompt_registry_capacity, and it aborts rather than
// returning false, so a silently missing prompt is impossible.
bool register_prompt(std::string_view name, std::string_view help_summary, exec_fn execute_fn,
                     void (*render_help_fn)(std::ostream&), completion_fn complete_fn = nullptr,
                     chain_role role = chain_role::normal, bool stream_output = true, preflight_fn preflight = nullptr,
                     declare_fn declare = nullptr) noexcept;

// Register a sink: the same registry entry with role=sink. Sink flags
// (--on-error/--with-context/--show-context/--head) are handled by the
// dispatcher through sdk/sink.hpp, so adding a sink only needs its own
// executor, help renderer, and stream flag.
bool register_sink(std::string_view name, std::string_view help_summary, exec_fn execute_fn,
                   void (*render_help_fn)(std::ostream&), completion_fn complete_fn = nullptr,
                   bool stream_output = true, preflight_fn preflight = nullptr, declare_fn declare = nullptr) noexcept;

// Captures a prompt's own `render_help_*` output into a string, for prompts
// that append it to a failure message (cli, man, tse.analyze, tse.intraday)
// instead of printing it directly (`help_text(render_help_cli)` instead of the
// ostringstream dance).
template <typename RenderFn>
std::string help_text(RenderFn render) {
    std::ostringstream os;
    render(os);
    return os.str();
}

} // namespace prompt
