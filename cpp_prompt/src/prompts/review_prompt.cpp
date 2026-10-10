#include "prompt/prompts/review_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_review_prompt_config {
    std::size_t head_lines = 0;
    std::vector<std::string_view> files;
};

basic_review_prompt_config parse_review_args(std::span<std::string_view const> args) noexcept {
    basic_review_prompt_config config;
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

prompt_result execute_review(prompt_context&& ctx) noexcept {
    auto config = parse_review_args({ctx.args.data(), ctx.args_count});
    
    std::string output;
    output += "Review the following code for:\n";
    output += "1. Correctness — logic errors, edge cases, null/undefined behavior\n";
    output += "2. Style — naming, formatting, consistency with project conventions\n";
    output += "3. Performance — unnecessary allocations, algorithmic complexity, I/O patterns\n";
    output += "4. Security — input validation, injection risks, secrets exposure\n";
    output += "5. Maintainability — coupling, abstraction levels, testability\n\n";
    output += "Provide specific, actionable feedback with line references where possible.\n\n";
    
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

void render_help_review(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt review [--head N] [FILE...]
       some-command | prompt review [--head N] [FILE...]

Review code for correctness, style, performance, and security issues.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts