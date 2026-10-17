#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <regex>
#include <string>

namespace prompt::prompts {

namespace {

// Port of cpp.sh is_compiler_output: `grep -qiE` with these patterns, so the
// match is case-insensitive and never spans a newline.
bool is_compiler_output(std::string_view content) {
    static const std::regex compiler_regex(
        R"(:[0-9]+:([0-9]+:)? (error|warning|fatal error|note):|In file included from|undefined reference to|no matching function for call to|ld: symbol\(s\) not found|FAILED:|ninja: build stopped|make\[[0-9]+\]: \*\*\*)",
        std::regex_constants::icase);
    if (content.empty()) return false;
    return std::regex_search(std::string(content), compiler_regex);
}

std::string strip_trailing_newlines(std::string_view content) {
    while (!content.empty() && content.back() == '\n') content.remove_suffix(1);
    return std::string(content);
}

} // namespace

static void execute_cpp(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);
    (void)head_lines; // cpp.sh never embeds files, so --head has no effect

    std::string content = ctx.stdin_consumed ? strip_trailing_newlines(ctx.stdin_content) : std::string{};

    std::string output;
    if (!content.empty()) {
        if (!content.contains("```")) {
            output += "```\n";
            output += content + "\n\n";
            output += "```\n";
        } else {
            output += content + "\n\n";
        }
    }

    if (!content.empty() && is_compiler_output(content)) {
        output += R"PROMPT(
The provided output contains C++ compiler errors or warnings.
Please analyze the error messages and the provided code context.
Find the root problem and propose the smallest useful fix.
Explain the issue briefly, and provide the answer primarily as a git diff that can be applied directly.
Prefer minimal, surgical changes over broad rewrites.
)PROMPT";
    } else {
        output +=
            R"PROMPT(You are an expert C++ developer with deep knowledge of modern C++ (C++17, C++20, C++23, C++26) and strict adherence to the C++ Core Guidelines.
Follow these principles when providing C++ code:

- Use modern C++ features (auto, range-based loops, structured bindings, concepts, modules)
- Apply C++ Core Guidelines (ES, SL, NL, F, C, Enum, Con, T, I, R, Pro, E, S, Cp)
- Prioritize performance using move semantics, perfect forwarding, and minimal allocations
)PROMPT";
        output +=
            "- Minimize dependencies - prefer standard library over third-party libraries\n- Use constexpr for "
            "compile-time computation when possible\n- Apply [[nodiscard]] to functions whose return values should not "
            "be ignored\n- Prefer RAII and smart pointers for resource management\n- Leverage templates and concepts "
            "for generic programming\n- Write efficient, safe, readable, and maintainable code\n";
    }
    output += '\n';

    ctx.out = std::move(output);
}

static void render_help_cpp(std::ostream& os) {
    os << R"EOF(Usage: prompt cpp [--head N] [FILE...]
       make 2>&1 | prompt cpp [--head N] [FILE...]

Builds a prompt for generating or editing C++ code.
If C++ compiler errors are detected on stdin, it automatically simplifies the prompt to focus on fixing the compilation issues.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "cpp",
    "Builds a prompt for generating or editing C++ code. If C++ compiler errors are detected on "
    "stdin, it automatically simplifies the prompt to focus on fixing the compilation issues.",
    &execute_cpp, &render_help_cpp);

} // namespace prompt::prompts
