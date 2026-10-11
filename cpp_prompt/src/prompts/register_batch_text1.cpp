#include "prompt/prompts/api_prompt.hpp"
#include "prompt/prompts/bigo_prompt.hpp"
#include "prompt/prompts/branchless_prompt.hpp"
#include "prompt/prompts/comments_prompt.hpp"
#include "prompt/prompts/compile_time_prompt.hpp"
#include "prompt/prompts/docstring_prompt.hpp"
#include "prompt/prompts/explain_prompt.hpp"
#include "prompt/prompts/hoist_if_prompt.hpp"
#include "prompt/prompts/perf_prompt.hpp"
#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/prompts/security_prompt.hpp"

namespace prompt {

void register_batch_text1() noexcept {
    register_prompt({"api", "Review this API design.", "", prompts::execute_api, prompts::render_help_api});
    register_prompt({"explain", "Explain this clearly and concretely.", "", prompts::execute_explain,
                     prompts::render_help_explain});
    register_prompt(
        {"perf", "Review this for performance issues.", "", prompts::execute_perf, prompts::render_help_perf});
    register_prompt({"security", "Review this for security and safety issues.", "", prompts::execute_security,
                     prompts::render_help_security});
    register_prompt({"comments", "Improve the comments and inline documentation here.", "", prompts::execute_comments,
                     prompts::render_help_comments});
    register_prompt({"compile-time", "Review C++ code for opportunities to reduce compile time.", "",
                     prompts::execute_compile_time, prompts::render_help_compile_time});
    register_prompt({"docstring",
                     "Asks the AI to add or improve docstrings for functions, classes, and methods "
                     "in the provided code.",
                     "", prompts::execute_docstring, prompts::render_help_docstring});
    register_prompt({"bigo",
                     "Asks the AI to calculate the Big O time and space complexity of the provided "
                     "algorithms.",
                     "", prompts::execute_bigo, prompts::render_help_bigo});
    register_prompt({"branchless", "Rewrite this C++ code to be branchless.", "", prompts::execute_branchless,
                     prompts::render_help_branchless});
    register_prompt({"hoist-if", "Hoist invariant if statements out of loops for performance.", "",
                     prompts::execute_hoist_if, prompts::render_help_hoist_if});
}

} // namespace prompt
