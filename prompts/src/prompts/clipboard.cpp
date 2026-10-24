#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_clipboard(view const& v, result& r) {
    auto head_lines = head_lines_or_throw(v);

    // bash: content="$(clipboard_content)" — the command substitution strips
    // trailing newlines, so an all-whitespace clipboard reads as empty.
    std::string content = clipboard_content(v.env);
    while (!content.empty() && content.back() == '\n') content.pop_back();

    if (content.empty()) {
        throw soft_error{"prompt clipboard: Clipboard is empty or unavailable.\n"};
    }

    r.out = trim_context_nl(content, head_lines);
}

static void render_help_clipboard(std::ostream& os) {
    os << R"EOF(Usage: prompt clipboard [--head N]

Read from the clipboard and embed the content as context for the AI.

Options:
  --head N   Keep only the first N lines of the embedded content
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("clipboard", "Read from the clipboard and embed the content as context for the AI.",
                            &execute_clipboard, &render_help_clipboard);

} // namespace prompt::prompts
