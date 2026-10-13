#include "../core/strings.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <string>

namespace prompt::prompts {

prompt_output execute_note(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    bool prepend = false;
    std::vector<std::string_view> notes;

    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head" && i + 1 < ctx.args_count) {
            head_lines = prompt::text::to_size(ctx.args[++i], 0); // unvalidated in bash, never read
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
        // bash's `embed_stdin || true` prints a blank block for an empty
        // (but piped) stdin too — note.sh relies on that separator.
        if (ctx.stdin_consumed) {
            output += embed_stdin(ctx.stdin_content, head_lines);
        }
    } else {
        // bash's `embed_stdin || true` prints a blank block for an empty
        // (but piped) stdin too — note.sh relies on that separator.
        if (ctx.stdin_consumed) {
            output += embed_stdin(ctx.stdin_content, head_lines);
        }
        if (!notes.empty()) output += "\n";
        for (auto note : notes) {
            output += std::string(note) + "\n\n";
        }
    }

    return {std::move(output), std::string{}, false, ctx.stdin_consumed};
}

void render_help_note(std::ostream& os) {
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