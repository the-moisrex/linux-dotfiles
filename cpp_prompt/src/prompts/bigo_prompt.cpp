#include "prompt/prompts/bigo_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <unistd.h>

namespace prompt::prompts {

prompt_result execute_bigo(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Calculate the Big O notation for the time and space complexity of the following "
              "algorithms.\n";
    output += "Provide a clear step-by-step breakdown of your reasoning.\n";
    output += "Explicitly define what any variables (e.g., N, M, V, E) represent in the context "
              "of the inputs.\n";
    output += "If there are multiple functions or methods, analyze each one individually.\n";
    output += "ALWAYS format your Big O notations using math delimiters, such as "
              "\\mathcal{O}(N) or \\mathcal{O}(N \\log N).\n";
    output += "\n";

    // bigo.sh guards its fzf fallback with `[ $# -eq 0 ]`, and because
    // parse_arguments shifts only its own (function-local) positional
    // parameters, $# here is the ORIGINAL argument count — flags included.
    // Its output is stored in $@ but the loop iterates ARGS, so the fzf
    // result is discarded; running select_files only reproduces fzf's
    // stderr ("inappropriate ioctl for device" without a tty).
    if (ctx.args_count == 0) {
        (void)ctx.select_files();
    }

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        auto resolved = ctx.resolve_input_file(ctx.args[i]);
        if (!resolved) {
            error += "prompt bigo: file not found: " + std::string(ctx.args[i]) + "\n";
            continue;
        }
        if (::access(resolved->c_str(), R_OK) != 0) {
            error += "prompt bigo: file not readable: \"" + std::string(ctx.args[i]) + "\"; resolved to \"" +
                     resolved->string() + "\"\n";
            continue;
        }
        output += "File: " + resolved->filename().string() + "\n\n";
        output += "```" + infer_lang(*resolved) + "\n";
        output += trim_context_nl(read_file(*resolved), head_lines);
        output += "\n```\n";
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_bigo(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt bigo [--head N] [FILE...]
       some-command | prompt bigo [--head N] [FILE...]

Asks the AI to calculate the Big O time and space complexity of the provided algorithms.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
