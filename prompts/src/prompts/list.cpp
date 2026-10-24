#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

static void execute_list(view const& v, result& r) {
    // This prompt embeds no files, so --head is validated (and stripped) but
    // its value is never used.
    head_lines_or_throw(v);

    bool names_only = false;
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        if (v.args[i] == "--head") {
            ++i;
        } else if (v.args[i] == "--names") {
            names_only = true;
        } else {
            throw usage_error{"prompt list: unknown argument: " + std::string(v.args[i]) + "\n", {}, true};
        }
    }

    // Sort by name (shared with list-prompts/delegate).
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
        bool is_tty = isatty(STDOUT_FILENO) != 0;

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

    r.out = std::move(output);
    r.no_clipboard = true;
}

static void render_help_list(std::ostream& os) {
    os << R"EOF(Usage: prompt list [--names]

List all available prompts with a short description.

The description is the first paragraph of the prompt's --help output,
joined into one line.

Options:
  --names        Print only prompt names, one per line (fast: skips --help)
  -h, --help     Show this help
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("list", "List all available prompts with a short description.",
                                                &execute_list, &render_help_list);

} // namespace prompt::prompts