#include "../sdk/prompt_context.hpp"
#include "../sdk/prompt_registry.hpp"
#include <ostream>
#include <string>

namespace prompt::prompts {

static void render_help_clear(std::ostream& os);

static void execute_clear(prompt_context& ctx) {
    if (ctx.args_count != 0) {
        throw usage_error{"prompt clear: does not take arguments\n" + help_text(render_help_clear)};
    }

    // A silent passthrough-like sink: it emits nothing, and only asks the
    // dispatcher to drop the chain state later segments would inherit.
    ctx.no_clipboard = true;
    ctx.clear_context = true;
}

static void render_help_clear(std::ostream& os) {
    os << R"EOF(Usage: prompt <other prompts...> .clear

Reset the accumulated chain context so later prompts and sinks start a new,
independent section. Output already printed and the clipboard are kept; what is
discarded is everything a later sink would otherwise inherit: the accumulated
chain text, the filename hint, the redaction note and the source-batch failure
taint. Piped stdin that has not been consumed yet is discarded as well.

`.flush [FILE|DIR] [options]` is `.save` followed by `.clear`: it writes the
context to disk first and clears only if the write succeeds.

Examples:
  prompt .tse khodro .ai.grok .save khodro.md .clear .tse foolad .ai.grok .save foolad.md
  prompt .note "first task" .ai.grok .clear .note "unrelated second task" .ai.grok
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_sink(
    "clear", "Reset the accumulated chain context so later prompts and sinks start a new, independent section.",
    &execute_clear, &render_help_clear, nullptr, prompt::sink_policy::chain, false);

} // namespace prompt::prompts
