#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_list_prompts(prompt_context& ctx) {
    // list-prompts.sh runs init_prompt --no-files, so parse_arguments validates
    // and strips --head before the script's "takes no arguments" check; the
    // value is never used.
    head_lines_or_throw(ctx);
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
        } else {
            throw usage_error{"prompt list-prompts: takes no arguments\n"};
        }
    }
    auto prompts = get_all_prompts();

    // Sort by name (the bash list-prompts prompt sorts too).
    std::vector<prompt_descriptor const*> sorted;
    sorted.reserve(prompts.size());
    for (auto const& desc : prompts) sorted.push_back(&desc);
    std::sort(sorted.begin(), sorted.end(), prompt_descriptor_less);

    std::string output;

    for (auto const* desc : sorted) {
        output += std::format("=== {} ===\n", std::string(desc->name));

        std::string body;
        if (desc->render_help_fn != nullptr) {
            std::ostringstream oss;
            desc->render_help_fn(oss);
            body = oss.str();
        }
        if (body.empty()) body = "(no help)";
        if (body.back() != '\n') body += '\n';
        output += body;
        output += '\n';
    }

    ctx.out = std::move(output);
    ctx.no_clipboard = true;
}

static void render_help_list_prompts(std::ostream& os) {
    os << R"EOF(Usage: prompt list-prompts

Print the full help text of every available prompt, one after another.
Bash prompts are shown via their --help output; .txt/.md prompts are
printed in full.

See also: prompt list (short descriptions, one line per prompt).
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "list-prompts",
    "Print the full help text of every available prompt, one after another. Bash prompts are shown "
    "via their --help output; .txt/.md prompts are printed in full.",
    &execute_list_prompts, &render_help_list_prompts);

} // namespace prompt::prompts