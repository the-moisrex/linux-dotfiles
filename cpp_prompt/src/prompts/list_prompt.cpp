#include "prompt/prompts/list_prompt.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <iostream>
#include <string>
#include <unistd.h>

namespace prompt::prompts {

prompt_result execute_list(prompt_context&& ctx) noexcept {
    bool names_only = false;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--names") names_only = true;
    }
    
    auto prompts = get_all_prompts();
    std::string output;
    
    if (names_only) {
        for (auto const& desc : prompts) {
            output += std::string(desc.name) + "\n";
        }
    } else {
        bool is_tty = isatty(STDOUT_FILENO);
        
        if (is_tty) {
            output += "Prompt              Description\n";
            output += "------              -----------\n";
        }
        
        for (auto const& desc : prompts) {
            if (is_tty) {
                output += std::string(desc.name);
                output += std::string(18 - desc.name.size(), ' ');
                output += "  " + std::string(desc.help_summary) + "\n";
            } else {
                output += std::string(desc.name) + "\t" + std::string(desc.help_summary) + "\n";
            }
        }
    }
    
    return {std::move(output), 0, true};
}

void render_help_list(std::ostream& os) noexcept {
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