#include "../core/strings.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_note(view const& v, result& r) {
    std::optional<std::size_t> head_lines;
    bool prepend = false;
    std::vector<std::string_view> notes;

    for (std::size_t i = 0; i < v.args.size(); ++i) {
        if (v.args[i] == "--head") {
            // note.sh's own parser rejects a missing --head value with exit 2
            // (unlike its value, which it never validates or reads).
            if (i + 1 >= v.args.size()) throw usage_error{"Missing value for --head\n"};
            head_lines = prompt::text::to_size(v.args[++i], 0);
        } else if (v.args[i] == "--prepend" || v.args[i] == "-p") {
            prepend = true;
        } else {
            notes.push_back(v.args[i]);
        }
    }

    std::string output;

    if (prepend) {
        for (auto note : notes) {
            output += std::string(note) + "\n\n";
        }
        // bash's `embed_stdin || true` prints a blank block for an empty
        // (but piped) stdin too — note.sh relies on that separator.
        if (v.in.piped()) {
            output += embed_stdin(v.in.text(), head_lines);
        }
    } else {
        // bash's `embed_stdin || true` prints a blank block for an empty
        // (but piped) stdin too — note.sh relies on that separator.
        if (v.in.piped()) {
            output += embed_stdin(v.in.text(), head_lines);
        }
        if (!notes.empty()) output += '\n';
        for (auto note : notes) {
            output += std::string(note) + "\n\n";
        }
    }

    r.out = std::move(output);
}

static void render_help_note(std::ostream& os) {
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

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("note", "Add a note to the prompt.", &execute_note, &render_help_note);

} // namespace prompt::prompts