#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/signals.hpp"
#include <filesystem>
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

// A declaration (argv-only, pre-batch), so `.compress` in the same batch sees
// the intent without a sink barrier.
void declare_debug(declaration& d) {
    if (!d.prior.get(keys::mode)) d.declared.set(keys::mode, signals::to_string(signals::intent::debug));
    std::vector<std::string> const files = focus_files(d.args);
    if (!files.empty()) d.declared.set(keys::focus, signals::join_focus(files));
}

} // namespace

static void execute_debug(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(You are an expert debugger across all programming languages and runtimes.
Analyze the provided code, error messages, stack traces, or logs and identify the root cause of the issue.
)PROMPT";
    output +=
        "\nApproach:\n1. Identify the language, runtime, and environment from the context.\n2. Trace the error from "
        "its symptom back to its origin.\n3. Explain the root cause clearly and concisely.\n4. Provide the smallest "
        "useful fix as a git diff.\n5. If the issue is ambiguous, list the most likely causes ranked by "
        "probability.\n\nFocus on:\n- Actual bugs, not style issues\n- The first actionable fix rather than a list of "
        "everything wrong\n- Edge cases that could cause intermittent failures\n- Environmental issues (missing deps, "
        "wrong versions, path problems)\n- Race conditions, resource leaks, and off-by-one errors\n\n";

    std::string error;
    auto sources = collect_file_args(v);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        // debug.sh leaves a blank line before the closing fence.
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_debug(std::ostream& os) {
    os << file_usage_help("debug") << R"EOF(
General-purpose multi-language debugging prompt.
Analyzes error messages, stack traces, or suspicious code and identifies the root cause.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "debug",
    "General-purpose multi-language debugging prompt. Analyzes error messages, "
    "stack traces, or suspicious code and identifies the root cause.",
    &execute_debug, &render_help_debug, nullptr, chain_role::normal, true, nullptr, &declare_debug);

} // namespace prompt::prompts
