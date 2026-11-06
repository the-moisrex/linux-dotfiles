#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <string_view>

namespace prompt::prompts {

static void execute_simplify([[maybe_unused]] prompt::view const& v, prompt::result& r) {
    r.out = R"PROMPT(Simplify the code while preserving its existing behavior.
Look for unnecessary complexity, duplication, and opportunities to make the code easier to read and maintain.
Explain the key simplifications.

)PROMPT";
}

static void render_help_simplify(std::ostream& os) {
    os << prompt::file_usage_help("simplify") << R"EOF(
Simplify code while preserving its existing behavior and public interfaces.

Options:
)EOF" << prompt::file_options_help("the embedded context");
}

bool const registered =
    prompt::register_prompt("simplify", "Simplify code while preserving its existing behavior and public interfaces.",
                            &execute_simplify, &render_help_simplify);

} // namespace prompt::prompts
