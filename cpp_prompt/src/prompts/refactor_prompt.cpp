#include "prompt/prompts/refactor_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_refactor_prompt_config {
    std::size_t head_lines = 0;
    std::vector<std::string_view> files;
};

basic_refactor_prompt_config parse_refactor_args(std::span<std::string_view const> args) noexcept {
    basic_refactor_prompt_config config;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--head" && i + 1 < args.size()) {
            config.head_lines = std::stoull(std::string(args[++i]));
        } else {
            config.files.push_back(args[i]);
        }
    }
    return config;
}

} // namespace

prompt_result execute_refactor(prompt_context&& ctx) noexcept {
    auto config = parse_refactor_args({ctx.args.data(), ctx.args_count});
    
    std::string output;
    output += "Refactor the following code to improve:\n";
    output += "1. Readability — clear names, reduced complexity, better structure\n";
    output += "2. Performance — eliminate redundant work, better algorithms, caching\n";
    output += "3. Maintainability — separation of concerns, DRY, SOLID principles\n";
    output += "4. Modern practices — use language features, standard library, patterns\n\n";
    output += "Provide the refactored code with explanations of key changes.\n\n";
    
    if (ctx.stdin_consumed && !ctx.stdin_content.empty()) {
        output += embed_stdin(ctx.stdin_content, config.head_lines);
    }
    
    for (auto file_sv : config.files) {
        std::filesystem::path file(file_sv);
        if (std::filesystem::exists(file)) {
            output += embed_file(file, file.filename().string(), config.head_lines);
        }
    }
    
    return {std::move(output), 0, false};
}

void render_help_refactor(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt refactor [--head N] [FILE...]
       some-command | prompt refactor [--head N] [FILE...]

Refactor the given code for clarity, performance, and maintainability.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts