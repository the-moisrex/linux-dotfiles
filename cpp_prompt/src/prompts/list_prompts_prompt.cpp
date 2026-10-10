#include "prompt/prompts/list_prompts_prompt.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <sstream>
#include <string>

namespace prompt::prompts {

prompt_result execute_list_prompts(prompt_context&& ctx) noexcept {
    (void)ctx;
    auto prompts = get_all_prompts();
    std::string output;
    
    for (auto const& desc : prompts) {
        output += std::string(desc.name) + "\n";
        output += std::string(desc.name.size(), '-') + "\n\n";
        
        std::ostringstream oss;
        desc.render_help_fn(oss);
        output += oss.str();
        output += "\n\n";
    }
    
    return {std::move(output), 0, true};
}

void render_help_list_prompts(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt list-prompts

Print the full help text of every available prompt.
)EOF";
}

} // namespace prompt::prompts