#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <format>
#include <string>

namespace prompt::prompts {

static void execute_cpp_minimal_allocations(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Reduce the number of heap allocations in this code to the practical minimum.
Rewrite each allocation site so it is either eliminated or amortised:
- reserve containers and strings once up front instead of growing them;
- reuse buffers instead of allocating fresh ones per iteration or call;
- pass std::string_view / std::span and take arguments by reference instead of copying;
- prefer move semantics and in-place construction over temporaries;
- prefer stack or fixed-capacity storage where the bounds are known.

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

static void render_help_cpp_minimal_allocations(std::ostream& os) {
    os << file_usage_help("cpp.minimal-allocations") << R"EOF(
Ask the AI to rewrite this code to use as few heap allocations as possible.

Options:
)EOF" << file_options_help("the embedded context");
}

bool const registered = prompt::register_prompt(
    "cpp.minimal-allocations",
    "Ask the AI to rewrite this code to use as few heap allocations as possible.",
    &execute_cpp_minimal_allocations, &render_help_cpp_minimal_allocations);

} // namespace prompt::prompts