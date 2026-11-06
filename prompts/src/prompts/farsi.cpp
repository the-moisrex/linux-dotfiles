#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_farsi([[maybe_unused]] view const& v, result& r) {
    r.out = "Translate to Farsi; no explanations, no comments, no extra text, just the translation.\n\n";
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
