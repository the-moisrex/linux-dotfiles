#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_debug(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

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
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
        // debug.sh leaves a blank line before the closing fence.
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_debug(std::ostream& os) {
    os << R"EOF(Usage: prompt debug [--head N] [FILE...]
       some-command | prompt debug [--head N] [FILE...]

General-purpose multi-language debugging prompt.
Analyzes error messages, stack traces, or suspicious code and identifies the root cause.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("debug",
                            "General-purpose multi-language debugging prompt. Analyzes error messages, "
                            "stack traces, or suspicious code and identifies the root cause.",
                            &execute_debug, &render_help_debug);

} // namespace prompt::prompts
