#include "../sdk/prompt_registry.hpp"
#include "tse_find_impl.hpp"

namespace prompt::prompts {

static void execute_tse_find(view const& v, result& r) { execute_tse_find_impl(v, r); }

static void render_help_tse_find(std::ostream& os) { render_help_tse_find_impl(os); }

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "tse.find",
    "Turn a plain-language market request (\"what to buy tomorrow\", \"oversold stocks with heavy volume\", "
    "\"undervalued banks\") into a `tse.find` screening command for the Tehran Stock Exchange, or review a "
    "screen "
    "that already ran: pass a saved `.tse` snapshot, `tse.find` flags to run now, or pipe `tse.find` output "
    "(tse/json/jsonl/table) and the AI ranks the matches with the real numbers and suggests deep-dive prompts. "
    "Chat mode (default) makes the AI emit exactly one command it cannot run itself; agentic mode makes it "
    "execute the command and relax filters until the screen returns matches.",
    &execute_tse_find, &render_help_tse_find);

} // namespace prompt::prompts
