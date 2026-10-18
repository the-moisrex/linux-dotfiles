#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_compile_time(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output;
    output +=
        "Review this C++ code and identify easy fixes to reduce compile time.\nLook for:\n- Unnecessary `#include` "
        "directives that can be removed.\n- Opportunities to replace `#include` with forward declarations.\n";
    output += R"PROMPT(- Inline functions or templates in headers that can be moved to source (.cpp) files.
- Template bloat or opportunities for explicit template instantiation.
- Expensive headers (e.g., `<iostream>`, `<regex>`, `<windows.h>`) included in header files instead of source files.
- Moving compile time constants inside a dependent templated out of that dependency.
- Reducing coupling of templated arguments.
- Minimizing template arguments and not packing unnecessary arguments.

Call out which issues matter most in practice, and provide a git diff for the high-impact, easy-to-implement improvements.
Do not sacrifice correctness or introduce severe runtime performance regressions for minor compile-time wins.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File not found or is not a regular file: {}\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_compile_time(std::ostream& os) {
    os << file_usage_help("compile-time") << R"EOF(
Review C++ code for opportunities to reduce compile time.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("compile-time", "Review C++ code for opportunities to reduce compile time.",
                            &execute_compile_time, &render_help_compile_time);

} // namespace prompt::prompts
