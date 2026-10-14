#include "../sdk/prompt_registry.hpp"
#include "prompt_entry.hpp"
#include "prompts.hpp"

namespace prompt {

void register_batch_text1() {
    register_prompt({
        .name = "api",
        .help_summary = "Review this API design.",
        .help_full = "",
        .execute_fn = prompts::execute_api,
        .render_help_fn = prompts::render_help_api,
    });
    register_prompt({
        .name = "explain",
        .help_summary = "Explain this clearly and concretely.",
        .help_full = "",
        .execute_fn = prompts::execute_explain,
        .render_help_fn = prompts::render_help_explain,
    });
    register_prompt({
        .name = "perf",
        .help_summary = "Review this for performance issues.",
        .help_full = "",
        .execute_fn = prompts::execute_perf,
        .render_help_fn = prompts::render_help_perf,
    });
    register_prompt({
        .name = "security",
        .help_summary = "Review this for security and safety issues.",
        .help_full = "",
        .execute_fn = prompts::execute_security,
        .render_help_fn = prompts::render_help_security,
    });
    register_prompt({
        .name = "comments",
        .help_summary = "Improve the comments and inline documentation here.",
        .help_full = "",
        .execute_fn = prompts::execute_comments,
        .render_help_fn = prompts::render_help_comments,
    });
    register_prompt({
        .name = "compile-time",
        .help_summary = "Review C++ code for opportunities to reduce compile time.",
        .help_full = "",
        .execute_fn = prompts::execute_compile_time,
        .render_help_fn = prompts::render_help_compile_time,
    });
    register_prompt({
        .name = "docstring",
        .help_summary = "Asks the AI to add or improve docstrings for functions, classes, and methods "
                        "in the provided code.",
        .help_full = "",
        .execute_fn = prompts::execute_docstring,
        .render_help_fn = prompts::render_help_docstring,
    });
    register_prompt({
        .name = "bigo",
        .help_summary = "Asks the AI to calculate the Big O time and space complexity of the provided "
                        "algorithms.",
        .help_full = "",
        .execute_fn = prompts::execute_bigo,
        .render_help_fn = prompts::render_help_bigo,
    });
    register_prompt({
        .name = "branchless",
        .help_summary = "Rewrite this C++ code to be branchless.",
        .help_full = "",
        .execute_fn = prompts::execute_branchless,
        .render_help_fn = prompts::render_help_branchless,
    });
    register_prompt({
        .name = "hoist-if",
        .help_summary = "Hoist invariant if statements out of loops for performance.",
        .help_full = "",
        .execute_fn = prompts::execute_hoist_if,
        .render_help_fn = prompts::render_help_hoist_if,
    });
}

} // namespace prompt
