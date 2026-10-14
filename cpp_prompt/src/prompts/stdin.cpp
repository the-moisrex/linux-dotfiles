#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <string>

namespace prompt::prompts {

prompt_output execute_stdin(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    // bash's `if ! embed_stdin` branch: no piped stdin is a hard error.
    if (!ctx.stdin_consumed) {
        throw soft_error{"prompt stdin: No input. Pipe something to stdin.\n"};
    }

    return {
        .text = embed_stdin(ctx.stdin_content, head_lines),
        .error = std::string{},
        .no_clipboard = false,
        .stdin_consumed = true,
    };
}

void render_help_stdin(std::ostream& os) {
    os << R"EOF(Usage: prompt stdin [--head N]

Read from stdin and embed the content as context for the AI.
Pipe something into this prompt to include it in the AI context.

Options:
  --head N   Keep only the first N lines of the embedded content
)EOF";
}

} // namespace prompt::prompts
