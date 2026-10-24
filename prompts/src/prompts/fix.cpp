#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_fix(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Find the root problem here and propose the smallest useful fix.
Explain the issue briefly, mention any important assumptions, and provide the answer primarily as a git diff that can be applied directly.
Prefer minimal, surgical changes over broad rewrites.

)PROMPT";

    std::string error;
    auto sources = collect_file_args(v);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_fix(std::ostream& os) {
    os << file_usage_help("fix") << R"EOF(
Find the root problem here and propose the smallest useful fix.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "fix", "Find the root problem here and propose the smallest useful fix.", &execute_fix, &render_help_fix);

} // namespace prompt::prompts
