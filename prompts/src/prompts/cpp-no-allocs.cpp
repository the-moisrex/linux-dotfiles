#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <format>
#include <string>

namespace prompt::prompts {

static void execute_cpp_no_allocations(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Rewrite this code so it performs no heap allocations at all. Every container,
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

    if (v.in.available()) {
        std::string_view const text = v.in.text();
        if (!text.empty()) {
            output += embed_stdin(text, opts.head_lines);
        }
        v.in.spend();
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_cpp_no_allocations(std::ostream& os) {
    os << file_usage_help("cpp.no-allocations") << R"EOF(
Ask the AI to rewrite this code so it performs no heap allocations.

Options:
)EOF" << file_options_help("the embedded context");
}

bool const registered = prompt::register_prompt(
    "cpp.no-allocations",
    "Ask the AI to rewrite this code so it performs no heap allocations.",
    &execute_cpp_no_allocations, &render_help_cpp_no_allocations);

} // namespace prompt::prompts