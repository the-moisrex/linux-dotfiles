#include "prompt/prompts/debug_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_debug(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "You are an expert debugger across all programming languages and runtimes.\n";
    output += "Analyze the provided code, error messages, stack traces, or logs and identify the "
              "root cause of the issue.\n";
    output += "\n";
    output += "Approach:\n";
    output += "1. Identify the language, runtime, and environment from the context.\n";
    output += "2. Trace the error from its symptom back to its origin.\n";
    output += "3. Explain the root cause clearly and concisely.\n";
    output += "4. Provide the smallest useful fix as a git diff.\n";
    output += "5. If the issue is ambiguous, list the most likely causes ranked by probability.\n";
    output += "\n";
    output += "Focus on:\n";
    output += "- Actual bugs, not style issues\n";
    output += "- The first actionable fix rather than a list of everything wrong\n";
    output += "- Edge cases that could cause intermittent failures\n";
    output += "- Environmental issues (missing deps, wrong versions, path problems)\n";
    output += "- Race conditions, resource leaks, and off-by-one errors\n";
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            // debug.sh leaves a blank line before the closing fence.
            output += "\n```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_debug(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt debug [--head N] [FILE...]
       some-command | prompt debug [--head N] [FILE...]

General-purpose multi-language debugging prompt.
Analyzes error messages, stack traces, or suspicious code and identifies the root cause.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
