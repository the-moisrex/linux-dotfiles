#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <string_view>

namespace prompt::prompts {

static void execute_cpp_modernize([[maybe_unused]] view const& v, result& r) {
    r.out = R"PROMPT(Modernize this C++ code to the C++ Core Guidelines and C++26.
Use constexpr and consteval where they preserve behaviour, and concepts to constrain templates instead of SFINAE or enable_if.
Prefer structured bindings, ranges, std::span, std::string_view, designated initializers, if constexpr, fold expressions, std::format, std::optional, std::variant, std::expected, std::bit_cast, std::to_underlying, and std::ssize over their older spellings.
Annotate hot and cold branches with [[likely]] and [[unlikely]] when the profile justifies it.
Replace raw ownership and index-based loops with RAII, ranges, and algorithms where that makes the intent clearer.
Keep semantics identical unless a change fixes a documented defect; list every non-mechanical change and why it is safe.
Answer with the modernized code in full, one fenced block per file, followed by a short rationale per non-trivial change.

)PROMPT";
}

static void render_help_cpp_modernize(std::ostream& os) {
    os << file_usage_help("cpp.modernize") << R"EOF(
Modernize this C++ code to the C++ Core Guidelines and C++26.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("cpp.modernize", "Modernize this C++ code to the C++ Core Guidelines and C++26.",
                            &execute_cpp_modernize, &render_help_cpp_modernize);

} // namespace prompt::prompts
