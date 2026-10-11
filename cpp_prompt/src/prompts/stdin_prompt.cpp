#include "prompt/prompts/stdin_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>

namespace prompt::prompts {

prompt_result execute_stdin(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    // bash's `if ! embed_stdin` branch: no piped stdin is a hard error.
    if (!ctx.stdin_consumed) {
        return {std::string{}, 1, false, "prompt stdin: No input. Pipe something to stdin.\n", false};
    }

    return {embed_stdin(ctx.stdin_content, head_lines), 0, false, std::string{}, true};
}

void render_help_stdin(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt stdin [--head N]

Read from stdin and embed the content as context for the AI.
Pipe something into this prompt to include it in the AI context.

Options:
  --head N   Keep only the first N lines of the embedded content
)EOF";
}

} // namespace prompt::prompts
