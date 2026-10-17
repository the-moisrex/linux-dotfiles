#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_farsi(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output;
    output += "Translate to Farsi; no explanations, no comments, no extra text, just the translation.\n\n";

    // bash's embed_stdin fails on a tty stdin, and since it is the script's
    // last command its status becomes the prompt's exit code (1).
    if (!ctx.stdin_consumed) throw soft_error{std::string{}, std::move(output)};

    output += embed_stdin(ctx.stdin_content, head_lines);
    ctx.out = std::move(output);
    ctx.stdin_consumed = true;
}

static void render_help_farsi(std::ostream& os) {
    os << R"EOF(Usage: prompt farsi [--head N]
       some-command | prompt farsi [--head N]

Translate to Farsi.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("farsi", "Translate to Farsi.", &execute_farsi, &render_help_farsi);

} // namespace prompt::prompts
