#include "prompt/prompts/clipboard_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>

namespace prompt::prompts {

prompt_result execute_clipboard(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    // bash: content="$(clipboard_content)" — the command substitution strips
    // trailing newlines, so an all-whitespace clipboard reads as empty.
    std::string content = ctx.clipboard_content();
    while (!content.empty() && content.back() == '\n') content.pop_back();

    if (content.empty()) {
        return {std::string{}, 1, false, "prompt clipboard: Clipboard is empty or unavailable.\n"};
    }

    return {trim_context_nl(content, head_lines), 0, false, std::string{}};
}

void render_help_clipboard(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt clipboard [--head N]

Read from the clipboard and embed the content as context for the AI.

Options:
  --head N   Keep only the first N lines of the embedded content
)EOF";
}

} // namespace prompt::prompts
