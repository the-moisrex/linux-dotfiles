#include "../sdk/prompt_registry.hpp"
#include "prompts.hpp"
#include <algorithm>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

prompt_output execute_list(prompt_context&& ctx) {
    bool names_only = false;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--names") {
            names_only = true;
        } else {
            throw usage_error{"prompt list: unknown argument: " + std::string(ctx.args[i]) + "\n", {}, true};
        }
    }

    // Sort by name (the bash list prompt sorts its collect_prompts output).
    auto prompts = get_all_prompts();
    std::vector<prompt_descriptor const*> sorted;
    sorted.reserve(prompts.size());
    for (auto const& desc : prompts) sorted.push_back(&desc);
    std::sort(sorted.begin(), sorted.end(), prompt_descriptor_less);

    std::string output;

    if (names_only) {
        for (auto const* desc : sorted) {
            output += std::string(desc->name) + "\n";
        }
    } else {
        bool is_tty = isatty(STDOUT_FILENO);

        if (is_tty) {
            output += "Prompt              Description\n------              -----------\n";
        }

        for (auto const* desc : sorted) {
            if (is_tty) {
                output += std::string(desc->name);
                output += std::string(18 - std::min<std::size_t>(18, desc->name.size()), ' ');
                output += std::format("  {}\n", std::string(desc->help_summary));
            } else {
                output += std::string(desc->name) + "\t" + std::string(desc->help_summary) + "\n";
            }
        }
    }

    return {std::move(output), std::string{}, true, false};
}

void render_help_list(std::ostream& os) {
    os << R"EOF(Usage: prompt list [--names]

List all available prompts with a short description.

The description is the first paragraph of the prompt's --help output,
joined into one line.

Options:
  --names        Print only prompt names, one per line (fast: skips --help)
  -h, --help     Show this help
)EOF";
}

} // namespace prompt::prompts