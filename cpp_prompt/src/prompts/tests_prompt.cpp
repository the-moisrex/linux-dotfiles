#include "prompt/prompts/tests_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_tests_prompt_config {
    std::size_t head_lines = 0;
    std::vector<std::string_view> files;
};

basic_tests_prompt_config parse_tests_args(std::span<std::string_view const> args) noexcept {
    basic_tests_prompt_config config;
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

prompt_result execute_tests(prompt_context&& ctx) noexcept {
    auto config = parse_tests_args({ctx.args.data(), ctx.args_count});
    
    std::string output;
    output += "Generate comprehensive unit tests for the following code.\n";
    output += "Cover: happy path, edge cases, error conditions, boundary values.\n";
    output += "Use the project's testing framework (Google Test, pytest, Jest, etc.).\n";
    output += "Follow existing test patterns and naming conventions.\n\n";
    
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

void render_help_tests(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt tests [--head N] [FILE...]
       some-command | prompt tests [--head N] [FILE...]

Generate comprehensive unit tests for the given code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts