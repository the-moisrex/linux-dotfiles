#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_cpp_minimal_allocations(view const& v, result& r) {
    r.out = R"PROMPT(Reduce the number of heap allocations in this code to the practical minimum.
Rewrite each allocation site so it is either eliminated or amortised:
- reserve containers and strings once up front instead of growing them;
- reuse buffers instead of allocating fresh ones per iteration or call;
- pass std::string_view / std::span and take arguments by reference instead of copying;
- prefer move semantics and in-place construction over temporaries;
- prefer stack or fixed-capacity storage where the bounds are known.

Keep observable behaviour identical. Return the answer primarily as a git diff
that can be applied directly; prefer minimal, surgical changes.

)PROMPT";
}

static void render_help_cpp_minimal_allocations(std::ostream& os) {
    os << file_usage_help("cpp.minimal-allocations") << R"EOF(
Ask the AI to rewrite this code to use as few heap allocations as possible.

Options:
)EOF" << file_options_help("the embedded context");
}

bool const registered = prompt::register_prompt(
    "cpp.minimal-allocations", "Ask the AI to rewrite this code to use as few heap allocations as possible.",
    &execute_cpp_minimal_allocations, &render_help_cpp_minimal_allocations);

} // namespace prompt::prompts
