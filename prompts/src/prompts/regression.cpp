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
// flags — enough to name the files under review as the chain's focus.
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

// A declaration, not an artifact: computed from argv alone and published before
// the batch forks, so a sibling `.compress` in the same batch reads the intent
// even without a sink barrier.
void declare_regression(declaration& d) {
    if (!d.prior.get(keys::mode)) d.declared.set(keys::mode, signals::to_string(signals::intent::review));
    std::vector<std::string> const files = focus_files(d.args);
    if (!files.empty()) d.declared.set(keys::focus, signals::join_focus(files));
}

} // namespace

static void execute_regression(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Find regressions likely introduced by these changes.
Focus on behavioral changes that could break existing behavior: altered output, changed error handling, shifted edge-case handling, broken invariants, or removed guarantees.
For each suspected regression, state the concrete before/after behavior and the scenario that would reveal it.
Prefer high-signal findings over speculation; say plainly when no likely regression is visible.

)PROMPT";

    if (v.in.piped()) {
        output += embed_stdin(v.in.text(), opts.head_lines);
        v.in.spend();
    }

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

static void render_help_regression(std::ostream& os) {
    os << file_usage_help("regression") << R"EOF(
Find regressions likely introduced by these changes.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("regression", "Find regressions likely introduced by these changes.", &execute_regression,
                            &render_help_regression, nullptr, chain_role::normal, true, nullptr, &declare_regression);

} // namespace prompt::prompts
