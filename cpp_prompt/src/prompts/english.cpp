#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <string>

namespace prompt::prompts {

prompt_output execute_english(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Translate to English; no explanations, no comments, no extra text, just the translation.\n\n";

    // bash's embed_stdin fails on a tty stdin, and since it is the script's
    // last command its status becomes the prompt's exit code (1).
    if (!ctx.stdin_consumed) throw soft_error{std::string{}, std::move(output)};

    output += embed_stdin(ctx.stdin_content, head_lines);
    return {std::move(output), std::string{}, false, true};
}

void render_help_english(std::ostream& os) {
    os << R"EOF(Usage: prompt english [--head N]
       some-command | prompt english [--head N]

Translate to English.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
