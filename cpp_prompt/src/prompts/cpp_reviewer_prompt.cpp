#include "prompt/prompts/cpp_reviewer_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

struct basic_cpp_reviewer_prompt_config {
    std::size_t head_lines = 0;
    std::vector<std::string_view> files;
};

basic_cpp_reviewer_prompt_config parse_cpp_reviewer_args(std::span<std::string_view const> args) noexcept {
    basic_cpp_reviewer_prompt_config config;
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

prompt_result execute_cpp_reviewer(prompt_context&& ctx) noexcept {
    auto config = parse_cpp_reviewer_args({ctx.args.data(), ctx.args_count});
    
    std::string output;
    output += "Perform a comprehensive C++ code review:\n\n";
    output += "1. Modern C++ — use of C++20/23/26 features, concepts, ranges, coroutines\n";
    output += "2. Resource management — RAII, smart pointers, no raw new/delete\n";
    output += "3. Performance — avoid copies, use move semantics, reserve containers\n";
    output += "4. Correctness — no UB, proper lifetimes, thread safety, strong exceptions\n";
    output += "5. Templates — concepts constraints, SFINAE, compilation speed\n";
    output += "6. Architecture — module structure, header hygiene, dependency direction\n\n";
    output += "Provide specific recommendations with code examples.\n\n";
    
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

void render_help_cpp_reviewer(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt cpp-reviewer [--head N] [FILE...]
       some-command | prompt cpp-reviewer [--head N] [FILE...]

Comprehensive C++ code review covering:
- Modern C++ usage (C++20/23/26 features, RAII, algorithms)
- Performance (allocations, copies, move semantics, cache efficiency)
- Correctness (UB, lifetime, thread safety, exception safety)
- Architecture (dependencies, coupling, abstractions, templates)
- Style (naming, formatting, project conventions)

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts