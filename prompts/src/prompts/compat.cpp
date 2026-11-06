// prompts/src/prompts/compat.cpp
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace prompt::prompts {

namespace {
inline constexpr std::string_view help_summary =
    "Find places where the code violates backward-compatibility.";
} // namespace

static void execute_compat(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Review this code for backward-compatibility violations.

Flag changes that would break existing users, callers, clients, or persisted data, such as:
- Removed or renamed public APIs, symbols, types, or headers
- Changed signatures: parameter or return types, argument order, requiredness, defaults
- Behavior changes to existing functions without a compatibility shim or version gate
- Removed or repurposed CLI flags, environment variables, config keys, or file formats
- Changed on-disk or wire formats (serialization, schemas, protocols) without versioning
- Tightened input validation or changed error conditions downstream code may rely on
- Changed defaults that silently alter behavior

For each finding:
- Cite the file and line (or symbol) so it can be located
- Classify impact: breaking / dangerous / deprecation-worthy
- Suggest a compatibility-preserving alternative when one exists (deprecation shim, feature flag, versioned format)

Do not flag deliberate breaking changes that are already behind a version bump or an explicit
breaking-change mechanism; note them as intentional instead.

Report findings as a compact list, most severe first.

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

    if (v.in.piped()) {
        std::string_view const in_text = v.in.text();
        v.in.spend();
        if (!in_text.empty()) {
            output += embed_stdin(in_text, opts.head_lines);
        }
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_compat(std::ostream& os) {
    os << file_usage_help("compat") << R"EOF(
Find places where the code violates backward-compatibility.

Options:
)EOF" << file_options_help("the embedded context");
}

bool const registered =
    prompt::register_prompt("compat", help_summary, &execute_compat, &render_help_compat);

} // namespace prompt::prompts
