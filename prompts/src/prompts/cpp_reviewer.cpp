#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_cpp_reviewer(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);
    (void)head_lines; // cpp-reviewer.sh only embeds stdin, which --head never trims

    std::string output;
    if (ctx.stdin_consumed) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }

    output +=
        R"PROMPT(You are an expert C++ code reviewer with deep knowledge of modern C++ (C++17, C++20, C++23, C++26) and strict adherence to the C++ Core Guidelines.
Follow these principles when reviewing C++ code:

- Check for compliance with C++ Core Guidelines (ES, SL, NL, F, C, Enum, Con, T, I, R, Pro, E, S, Cp)
- Identify potential performance issues (unnecessary copies, excessive allocations, pessimization)
- Verify proper use of modern C++ features (auto, range-based loops, structured bindings, concepts, modules)
- Check for proper exception safety and noexcept specifications
- Evaluate resource management and RAII compliance
- Look for opportunities to use constexpr for compile-time computation
- Identify missing [[nodiscard]] annotations on functions whose return values should not be ignored
- Verify proper use of smart pointers vs raw pointers
- Check for type safety and proper use of strong types and enums
- Identify potential memory leaks, dangling references, and undefined behavior
- Verify proper const-correctness and immutability where appropriate
- Look for adherence to the 'rule of zero/five' where applicable
- Check for proper template design and use of concepts for constraints
- Identify potential security vulnerabilities and unsafe operations
- Verify code is efficient, readable, maintainable, and follows best practices

Provide constructive feedback highlighting issues, suggesting improvements, and commending good practices.
)PROMPT";
    output += '\n';

    ctx.out = std::move(output);
}

static void render_help_cpp_reviewer(std::ostream& os) {
    os << R"EOF(Usage: prompt cpp-reviewer [--head N] [FILE...]
       some-command | prompt cpp-reviewer [--head N] [FILE...]

Builds a prompt for reviewing C++ code from stdin or embedded files.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("cpp-reviewer", "Builds a prompt for reviewing C++ code from stdin or embedded files.",
                            &execute_cpp_reviewer, &render_help_cpp_reviewer);

} // namespace prompt::prompts
