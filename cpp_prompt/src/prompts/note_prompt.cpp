#include "prompt/prompts/note_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>

namespace prompt::prompts {

prompt_result execute_note(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    bool prepend = false;
    std::vector<std::string_view> notes;
    
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head" && i + 1 < ctx.args_count) {
            head_lines = std::stoull(std::string(ctx.args[++i]));
        } else if (ctx.args[i] == "--prepend" || ctx.args[i] == "-p") {
            prepend = true;
        } else {
            notes.push_back(ctx.args[i]);
        }
    }
    
    std::string output;
    
    if (prepend) {
        for (auto note : notes) {
            output += std::string(note) + "\n\n";
        }
        if (ctx.stdin_consumed && !ctx.stdin_content.empty()) {
            output += embed_stdin(ctx.stdin_content, head_lines);
        }
    } else {
        if (ctx.stdin_consumed && !ctx.stdin_content.empty()) {
            output += embed_stdin(ctx.stdin_content, head_lines);
        }
        for (auto note : notes) {
            output += std::string(note) + "\n\n";
        }
    }
    
    return {std::move(output), 0, false};
}

void render_help_note(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt note [--head N] [NOTE...]
       some-command | prompt note [--head N] [NOTE...]

Add a note to the prompt.

The note is placed after the embedded context by default; use --prepend
to place it before everything.

Options:
  --head N      Keep only the first N lines of the embedded context
  --prepend|-p  Add before everything
)EOF";
}

} // namespace prompt::prompts