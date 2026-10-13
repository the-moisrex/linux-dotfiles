#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_output execute_debug(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "You are an expert debugger across all programming languages and runtimes.\n";
    output += "Analyze the provided code, error messages, stack traces, or logs and identify the "
              "root cause of the issue.\n";
    output +=
        "\nApproach:\n1. Identify the language, runtime, and environment from the context.\n2. Trace the error from "
        "its symptom back to its origin.\n3. Explain the root cause clearly and concisely.\n4. Provide the smallest "
        "useful fix as a git diff.\n5. If the issue is ambiguous, list the most likely causes ranked by "
        "probability.\n\nFocus on:\n- Actual bugs, not style issues\n- The first actionable fix rather than a list of "
        "everything wrong\n- Edge cases that could cause intermittent failures\n- Environmental issues (missing deps, "
        "wrong versions, path problems)\n- Race conditions, resource leaks, and off-by-one errors\n\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += std::format("File: {}\n\n", file.filename().string());
            output += std::format("```{}\n", infer_lang(file));
            output += trim_context_nl(read_file(file), head_lines);
            // debug.sh leaves a blank line before the closing fence.
            output += "\n```\n\n";
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    return {std::move(output), std::move(error), false, false};
}

void render_help_debug(std::ostream& os) {
    os << R"EOF(Usage: prompt debug [--head N] [FILE...]
       some-command | prompt debug [--head N] [FILE...]

General-purpose multi-language debugging prompt.
Analyzes error messages, stack traces, or suspicious code and identifies the root cause.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
