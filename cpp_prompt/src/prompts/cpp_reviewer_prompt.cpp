#include "prompt/prompts/cpp_reviewer_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>

namespace prompt::prompts {

prompt_result execute_cpp_reviewer(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }
    (void)head_lines; // cpp-reviewer.sh only embeds stdin, which --head never trims

    std::string output;
    if (ctx.stdin_consumed) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }

    output += "You are an expert C++ code reviewer with deep knowledge of modern C++ (C++17, "
              "C++20, C++23, C++26) and strict adherence to the C++ Core Guidelines.\n";
    output += "Follow these principles when reviewing C++ code:\n";
    output += "\n";
    output += "- Check for compliance with C++ Core Guidelines (ES, SL, NL, F, C, Enum, Con, T, I, "
              "R, Pro, E, S, Cp)\n";
    output += "- Identify potential performance issues (unnecessary copies, excessive allocations, "
              "pessimization)\n";
    output += "- Verify proper use of modern C++ features (auto, range-based loops, structured "
              "bindings, concepts, modules)\n";
    output += "- Check for proper exception safety and noexcept specifications\n";
    output += "- Evaluate resource management and RAII compliance\n";
    output += "- Look for opportunities to use constexpr for compile-time computation\n";
    output += "- Identify missing [[nodiscard]] annotations on functions whose return values "
              "should not be ignored\n";
    output += "- Verify proper use of smart pointers vs raw pointers\n";
    output += "- Check for type safety and proper use of strong types and enums\n";
    output += "- Identify potential memory leaks, dangling references, and undefined behavior\n";
    output += "- Verify proper const-correctness and immutability where appropriate\n";
    output += "- Look for adherence to the 'rule of zero/five' where applicable\n";
    output += "- Check for proper template design and use of concepts for constraints\n";
    output += "- Identify potential security vulnerabilities and unsafe operations\n";
    output += "- Verify code is efficient, readable, maintainable, and follows best practices\n";
    output += "\n";
    output += "Provide constructive feedback highlighting issues, suggesting improvements, and "
              "commending good practices.\n";
    output += "\n";

    return {std::move(output), 0, false, std::string{}, ctx.stdin_consumed};
}

void render_help_cpp_reviewer(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt cpp-reviewer [--head N] [FILE...]
       some-command | prompt cpp-reviewer [--head N] [FILE...]

Builds a prompt for reviewing C++ code from stdin or embedded files.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
