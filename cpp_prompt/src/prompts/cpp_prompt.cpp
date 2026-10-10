#include "prompt/prompts/cpp_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <regex>
#include <string>

namespace prompt::prompts {

namespace {

// Port of cpp.sh is_compiler_output: `grep -qiE` with these patterns, so the
// match is case-insensitive and never spans a newline.
bool is_compiler_output(std::string_view content) noexcept {
    static const std::regex compiler_regex(
        R"(:[0-9]+:([0-9]+:)? (error|warning|fatal error|note):|In file included from|undefined reference to|no matching function for call to|ld: symbol\(s\) not found|FAILED:|ninja: build stopped|make\[[0-9]+\]: \*\*\*)",
        std::regex_constants::icase);
    if (content.empty()) return false;
    return std::regex_search(std::string(content), compiler_regex);
}

std::string strip_trailing_newlines(std::string_view content) noexcept {
    while (!content.empty() && content.back() == '\n') content.remove_suffix(1);
    return std::string(content);
}

} // namespace

prompt_result execute_cpp(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }
    (void)head_lines; // cpp.sh never embeds files, so --head has no effect

    std::string content = ctx.stdin_consumed ? strip_trailing_newlines(ctx.stdin_content) : std::string{};

    std::string output;
    if (!content.empty()) {
        if (content.find("```") == std::string::npos) {
            output += "```\n";
            output += content + "\n\n";
            output += "```\n";
        } else {
            output += content + "\n\n";
        }
    }

    if (!content.empty() && is_compiler_output(content)) {
        output += "\n";
        output += "The provided output contains C++ compiler errors or warnings.\n";
        output += "Please analyze the error messages and the provided code context.\n";
        output += "Find the root problem and propose the smallest useful fix.\n";
        output += "Explain the issue briefly, and provide the answer primarily as a git diff that "
                  "can be applied directly.\n";
        output += "Prefer minimal, surgical changes over broad rewrites.\n";
    } else {
        output += "You are an expert C++ developer with deep knowledge of modern C++ (C++17, "
                  "C++20, C++23, C++26) and strict adherence to the C++ Core Guidelines.\n";
        output += "Follow these principles when providing C++ code:\n";
        output += "\n";
        output += "- Use modern C++ features (auto, range-based loops, structured bindings, "
                  "concepts, modules)\n";
        output += "- Apply C++ Core Guidelines (ES, SL, NL, F, C, Enum, Con, T, I, R, Pro, E, S, "
                  "Cp)\n";
        output += "- Prioritize performance using move semantics, perfect forwarding, and minimal "
                  "allocations\n";
        output += "- Minimize dependencies - prefer standard library over third-party libraries\n";
        output += "- Use constexpr for compile-time computation when possible\n";
        output += "- Apply [[nodiscard]] to functions whose return values should not be ignored\n";
        output += "- Prefer RAII and smart pointers for resource management\n";
        output += "- Leverage templates and concepts for generic programming\n";
        output += "- Write efficient, safe, readable, and maintainable code\n";
    }
    output += "\n";

    return {std::move(output), 0, false, std::string{}, ctx.stdin_consumed};
}

void render_help_cpp(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt cpp [--head N] [FILE...]
       make 2>&1 | prompt cpp [--head N] [FILE...]

Builds a prompt for generating or editing C++ code.
If C++ compiler errors are detected on stdin, it automatically simplifies the prompt to focus on fixing the compilation issues.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
