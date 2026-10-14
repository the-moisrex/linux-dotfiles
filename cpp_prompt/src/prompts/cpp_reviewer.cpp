#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <string>

namespace prompt::prompts {

prompt_output execute_cpp_reviewer(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }
    (void)head_lines; // cpp-reviewer.sh only embeds stdin, which --head never trims

    std::string output;
    if (ctx.stdin_consumed) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }

    output += "You are an expert C++ code reviewer with deep knowledge of modern C++ (C++17, "
              "C++20, C++23, C++26) and strict adherence to the C++ Core Guidelines.\n";
    output += "Follow these principles when reviewing C++ code:\n\n";
    output += "- Check for compliance with C++ Core Guidelines (ES, SL, NL, F, C, Enum, Con, T, I, "
              "R, Pro, E, S, Cp)\n";
    output += "- Identify potential performance issues (unnecessary copies, excessive allocations, "
              "pessimization)\n";
    output += "- Verify proper use of modern C++ features (auto, range-based loops, structured "
              "bindings, concepts, modules)\n";
    output += "- Check for proper exception safety and noexcept specifications\n- Evaluate resource management and "
              "RAII compliance\n- Look for opportunities to use constexpr for compile-time computation\n";
    output += "- Identify missing [[nodiscard]] annotations on functions whose return values "
              "should not be ignored\n";
    output += "- Verify proper use of smart pointers vs raw pointers\n- Check for type safety and proper use of strong "
              "types and enums\n- Identify potential memory leaks, dangling references, and undefined behavior\n- "
              "Verify proper const-correctness and immutability where appropriate\n- Look for adherence to the 'rule "
              "of zero/five' where applicable\n- Check for proper template design and use of concepts for "
              "constraints\n- Identify potential security vulnerabilities and unsafe operations\n- Verify code is "
              "efficient, readable, maintainable, and follows best practices\n\n";
    output += "Provide constructive feedback highlighting issues, suggesting improvements, and "
              "commending good practices.\n";
    output += "\n";

    return {
        .text = std::move(output),
        .error = std::string{},
        .no_clipboard = false,
        .stdin_consumed = ctx.stdin_consumed,
    };
}

void render_help_cpp_reviewer(std::ostream& os) {
    os << R"EOF(Usage: prompt cpp-reviewer [--head N] [FILE...]
       some-command | prompt cpp-reviewer [--head N] [FILE...]

Builds a prompt for reviewing C++ code from stdin or embedded files.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
