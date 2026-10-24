#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_stupid(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Find the stupid mistakes in this code.
Focus on obvious bugs, wrong assumptions, copy-paste errors, bad edge cases, misleading names, missing checks, and anything else that would make an experienced reviewer say 'well that was silly'.
Be blunt but useful. List each issue with a short explanation and the smallest practical fix.
At the end, suggest small git patches.

)PROMPT";

    std::string error;
    auto sources = collect_file_args(v);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        // stupid.sh leaves a blank line before the closing fence.
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_stupid(std::ostream& os) {
    os << file_usage_help("stupid") << R"EOF(
Find the stupid mistakes in this code.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("stupid", "Find the stupid mistakes in this code.", &execute_stupid, &render_help_stupid);

} // namespace prompt::prompts
