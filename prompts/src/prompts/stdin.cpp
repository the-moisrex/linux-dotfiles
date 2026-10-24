#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_stdin(view const& v, result& r) {
    auto head_lines = head_lines_or_throw(v);

    // bash's `if ! embed_stdin` branch: no piped stdin is a hard error.
    if (!v.in.piped()) {
        throw soft_error{"prompt stdin: No input. Pipe something to stdin.\n"};
    }

    r.out = embed_stdin(v.in.text(), head_lines);
    v.in.spend();
}

static void render_help_stdin(std::ostream& os) {
    os << R"EOF(Usage: prompt stdin [--head N]

Read from stdin and embed the content as context for the AI.
Pipe something into this prompt to include it in the AI context.

Options:
  --head N   Keep only the first N lines of the embedded content
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("stdin",
                            "Read from stdin and embed the content as context for the AI. Pipe something into "
                            "this prompt to include it in the AI context.",
                            &execute_stdin, &render_help_stdin);

} // namespace prompt::prompts
