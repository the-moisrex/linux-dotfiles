#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/signals.hpp"
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

// The FILE arguments, ignoring the `--head <value>` pair and the line-number
// flags — enough to name the files under repair as the chain's focus.
std::vector<std::string> focus_files(std::span<std::string_view const> args) {
    std::vector<std::string> files;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view const arg = args[i];
        if (arg == "--head") {
            ++i;
        } else if (is_number_flag(arg) || (arg.size() > 1 && arg.front() == '-')) {
            continue;
        } else {
            files.emplace_back(arg);
        }
    }
    return files;
}

// A declaration, not an artifact: it is computed from argv alone and runs before
// the batch forks, so `.compress` in the same batch reads the intent even
// without a sink barrier (parallel siblings never see each other's artifacts).
void declare_cpp_modernize(declaration& d) {
    if (!d.prior.get(keys::mode)) d.declared.set(keys::mode, signals::to_string(signals::intent::fix));
    std::vector<std::string> const files = focus_files(d.args);
    if (!files.empty()) d.declared.set(keys::focus, signals::join_focus(files));
}

} // namespace

static void execute_cpp_modernize(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Modernize this C++ code to the C++ Core Guidelines and C++26.
Use constexpr and consteval where they preserve behaviour, and concepts to constrain templates instead of SFINAE or enable_if.
Prefer structured bindings, ranges, std::span, std::string_view, designated initializers, if constexpr, fold expressions, std::format, std::optional, std::variant, std::expected, std::bit_cast, std::to_underlying, and std::ssize over their older spellings.
Annotate hot and cold branches with [[likely]] and [[unlikely]] when the profile justifies it.
Replace raw ownership and index-based loops with RAII, ranges, and algorithms where that makes the intent clearer.
Keep semantics identical unless a change fixes a documented defect; list every non-mechanical change and why it is safe.
Answer with the modernized code in full, one fenced block per file, followed by a short rationale per non-trivial change.

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
        if (!v.in.text().empty()) output += embed_stdin(v.in.text(), opts.head_lines);
        v.in.spend();
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_cpp_modernize(std::ostream& os) {
    os << file_usage_help("cpp.modernize") << R"EOF(
Modernize this C++ code to the C++ Core Guidelines and C++26.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "cpp.modernize", "Modernize this C++ code to the C++ Core Guidelines and C++26.", &execute_cpp_modernize,
    &render_help_cpp_modernize, nullptr, chain_role::normal, true, nullptr, &declare_cpp_modernize);

} // namespace prompt::prompts
