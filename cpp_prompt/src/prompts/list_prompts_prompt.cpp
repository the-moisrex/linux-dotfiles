#include "prompt/prompts/list_prompts_prompt.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_list_prompts(prompt_context&& ctx) noexcept {
    if (ctx.args_count > 0) {
        return {"prompt list-prompts: takes no arguments\n", 2, true};
    }
    auto prompts = get_all_prompts();

    // Sort by name (the bash list-prompts prompt sorts too).
    std::vector<prompt_descriptor const*> sorted;
    sorted.reserve(prompts.size());
    for (auto const& desc : prompts) sorted.push_back(&desc);
    std::sort(sorted.begin(), sorted.end(), prompt_descriptor_less);

    std::string output;

    for (auto const* desc : sorted) {
        output += "=== " + std::string(desc->name) + " ===\n";

        std::string body;
        if (!desc->help_full.empty()) {
            // Static help extracted from the script (or, for file-less
            // native prompts, the embedded fallback text).
            body = desc->help_full;
        } else if (desc->render_help_fn) {
            std::ostringstream oss;
            desc->render_help_fn(oss);
            body = oss.str();
        }
        if (body.empty()) body = "(no help)";
        if (body.back() != '\n') body += '\n';
        output += body;
        output += "\n";
    }

    return {std::move(output), 0, true};
}

void render_help_list_prompts(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt list-prompts

Print the full help text of every available prompt, one after another.
Bash prompts are shown via their --help output; .txt/.md prompts are
printed in full.

See also: prompt list (short descriptions, one line per prompt).
)EOF";
}

} // namespace prompt::prompts