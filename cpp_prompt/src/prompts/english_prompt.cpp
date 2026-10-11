#include "prompt/prompts/english_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>

namespace prompt::prompts {

prompt_result execute_english(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Translate to English; no explanations, no comments, no extra text, just the translation.\n";
    output += "\n";

    // bash's embed_stdin fails on a tty stdin, and since it is the script's
    // last command its status becomes the prompt's exit code (1).
    if (!ctx.stdin_consumed) return {std::move(output), 1, false, std::string{}, false};

    output += embed_stdin(ctx.stdin_content, head_lines);
    return {std::move(output), 0, false, std::string{}, true};
}

void render_help_english(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt english [--head N]
       some-command | prompt english [--head N]

Translate to English.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
