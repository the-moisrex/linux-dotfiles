#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <string_view>

namespace prompt::prompts {

static void execute_simplify(prompt::view const& v, prompt::result& r) {
    auto const opts = prompt::file_options_or_throw(v);

    std::string output = R"PROMPT(Simplify the code while preserving its existing behavior.
Look for unnecessary complexity, duplication, and opportunities to make the code easier to read and maintain.
Explain the key simplifications.

)PROMPT";

    std::string error;
    auto const sources = prompt::collect_file_args(v);

    for (auto const& file : sources.files) {
        output += prompt::embed_file_block(file, opts);
    }

    for (auto const arg : sources.missing) {
        prompt::warn_missing_file(error, arg);
    }

    if (v.in.piped()) {
        auto const input = v.in.text();
        v.in.spend();
        output += prompt::embed_stdin(input, opts.head_lines);
    }

    r.out = std::move(output);
    r.err = std::move(error);
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
