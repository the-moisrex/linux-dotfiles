#include "prompt/prompts/cpp_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_cpp_prompt_config {
    std::size_t head_lines = 0;
    std::vector<std::string_view> files;
};

basic_cpp_prompt_config parse_cpp_args(std::span<std::string_view const> args) noexcept {
    basic_cpp_prompt_config config;
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

prompt_result execute_cpp(prompt_context&& ctx) noexcept {
    auto config = parse_cpp_args({ctx.args.data(), ctx.args_count});
    
    std::string output;
    output += "Analyze the following C++ compiler/linker errors and provide fixes.\n";
    output += "For each error: explain the cause, show the fix, and explain why it works.\n";
    output += "If source files are provided, show the corrected code in context.\n\n";
    
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

void render_help_cpp(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt cpp [--head N] [FILE...]
       compiler-output | prompt cpp [--head N] [FILE...]

Analyze C++ compiler/linker errors and suggest fixes.
Auto-detects GCC/Clang error format from stdin.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts