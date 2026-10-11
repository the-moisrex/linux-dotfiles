#include "prompt/prompts/compile_time_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_compile_time(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Review this C++ code and identify easy fixes to reduce compile time.\n";
    output += "Look for:\n";
    output += "- Unnecessary `#include` directives that can be removed.\n";
    output += "- Opportunities to replace `#include` with forward declarations.\n";
    output += "- Inline functions or templates in headers that can be moved to source (.cpp) "
              "files.\n";
    output += "- Template bloat or opportunities for explicit template instantiation.\n";
    output += "- Expensive headers (e.g., `<iostream>`, `<regex>`, `<windows.h>`) included in "
              "header files instead of source files.\n";
    output += "- Moving compile time constants inside a dependent templated out of that "
              "dependency.\n";
    output += "- Reducing coupling of templated arguments.\n";
    output += "- Minimizing template arguments and not packing unnecessary arguments.\n";
    output += "\n";
    output += "Call out which issues matter most in practice, and provide a git diff for the "
              "high-impact, easy-to-implement improvements.\n";
    output += "Do not sacrifice correctness or introduce severe runtime performance regressions "
              "for minor compile-time wins.\n";
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            output += "\n```\n";
            output += "\n";
        } else {
            error += "Warning: File not found or is not a regular file: " + std::string(ctx.args[i]) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_compile_time(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt compile-time [--head N] [FILE...]
       some-command | prompt compile-time [--head N] [FILE...]

Review C++ code for opportunities to reduce compile time.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
