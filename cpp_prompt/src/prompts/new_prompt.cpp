#include "prompt/prompts/new_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_new_prompt_config {
    std::size_t head_lines = 0;
    std::string name;
    std::vector<std::string_view> args;
};

basic_new_prompt_config parse_new_args(std::span<std::string_view const> args) noexcept {
    basic_new_prompt_config config;
    bool seen_name = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head" && i + 1 < args.size()) {
            // Handled by dispatcher
        } else if (!seen_name && !args[i].starts_with('-')) {
            config.name = std::string(args[i]);
            seen_name = true;
        } else {
            config.args.push_back(args[i]);
        }
    }
    return config;
}

} // namespace

prompt_result execute_new(prompt_context&& ctx) noexcept {
    auto config = parse_new_args({ctx.args.data(), ctx.args_count});
    
    std::string description;
    if (config.args.size() > 0) {
        for (std::size_t i = 0; i < config.args.size(); ++i) {
            if (i) description += " ";
            description += config.args[i];
        }
    } else if (ctx.stdin_consumed) {
        description = ctx.stdin_content;
    }
    
    if (config.name.empty()) {
        return {"prompt new: no prompt name specified\n", 2, false};
    }
    
    std::string output;
    output += "Generate a new prompt module for: " + config.name + "\n\n";
    output += "Description: " + (description.empty() ? "(none provided)" : description) + "\n\n";
    output += "Output the complete C++ module (.cpp) following this template:\n\n";
    
    // Template with different delimiter to avoid conflicts
    output += "```cpp\n";
    output += "#include \"prompt/prompts/NAME_prompt.hpp\"\n";
    output += "#include \"prompt/sdk/embed.hpp\"\n\n";
    output += "namespace prompt::prompts {\n\n";
    output += "namespace {\n\n";
    output += "struct basic_NAME_prompt_config {\n";
    output += "    std::size_t head_lines = 0;\n";
    output += "};\n\n";
    output += "basic_NAME_prompt_config parse_NAME_args(std::span<std::string_view const> args) noexcept {\n";
    output += "    basic_NAME_prompt_config config;\n";
    output += "    for (std::size_t i = 0; i < args.size(); ++i) {\n";
    output += "        if (args[i] == \"--head\" && i + 1 < args.size()) {\n";
    output += "            config.head_lines = std::stoull(std::string(args[++i]));\n";
    output += "        }\n";
    output += "    }\n";
    output += "    return config;\n";
    output += "}\n\n";
    output += "} // namespace\n\n";
    output += "prompt_result execute_NAME(prompt_context&& ctx) noexcept {\n";
    output += "    auto config = parse_NAME_args({ctx.args.data(), ctx.args_count});\n\n";
    output += "    std::string output;\n";
    output += "    output += \"TODO: Implement prompt logic\\n\\n\";\n\n";
    output += "    if (ctx.stdin_consumed && !ctx.stdin_content.empty()) {\n";
    output += "        output += embed_stdin(ctx.stdin_content, config.head_lines);\n";
    output += "    }\n\n";
    output += "    return {std::move(output), 0, false};\n";
    output += "}\n\n";
    output += "void render_help_NAME(std::ostream& os) noexcept {\n";
    output += "    os << R\"EOF(\n";
    output += "Usage: prompt NAME [--head N] [FILE...]\n";
    output += "       some-command | prompt NAME [--head N] [FILE...]\n\n";
    output += "TODO: Add description\n\n";
    output += "Options:\n";
    output += "  --head N   Keep only the first N lines of the embedded context\n";
    output += ")EOF\";\n";
    output += "}\n\n";
    output += "} // namespace prompt::prompts\n";
    output += "```\n";
    
    return {std::move(output), 0, false};
}

void render_help_new(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt new [--head N] <prompt-name> [description...]

Generate a new prompt script from a natural language description.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts