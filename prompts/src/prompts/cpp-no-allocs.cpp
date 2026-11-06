#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_cpp_no_allocations([[maybe_unused]] view const& v, result& r) {
    r.out = R"PROMPT(Rewrite this code so it performs no heap allocations at all. Every container,
buffer, and string that currently allocates must become one of:
- a fixed-capacity stack type (e.g. std::array or an inline/preallocated buffer);
- an object-embedded, non-allocating container;
- caller-supplied storage passed by reference (std::span, out-parameters).

If a code path genuinely cannot avoid allocating (truly dynamic size, an API
forced to return a heap object, etc.), replace it with a non-allocating
interface — accept a caller-owned buffer, return a span over caller storage —
and note the constraint in a short comment rather than silently leaving an
allocation in place.

Keep observable behaviour identical. Return the answer primarily as a git diff
that can be applied directly; prefer minimal, surgical changes.

)PROMPT";
}

static void render_help_cpp_no_allocations(std::ostream& os) {
    os << file_usage_help("cpp.no-allocations") << R"EOF(
Ask the AI to rewrite this code so it performs no heap allocations.

Options:
)EOF" << file_options_help("the embedded context");
}

bool const registered =
    prompt::register_prompt("cpp.no-allocations", "Ask the AI to rewrite this code so it performs no heap allocations.",
                            &execute_cpp_no_allocations, &render_help_cpp_no_allocations);

} // namespace prompt::prompts
