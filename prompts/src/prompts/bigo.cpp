#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <unistd.h>

namespace prompt::prompts {

static void execute_bigo(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output =
        R"PROMPT(Calculate the Big O notation for the time and space complexity of the following algorithms.
Provide a clear step-by-step breakdown of your reasoning.
Explicitly define what any variables (e.g., N, M, V, E) represent in the context of the inputs.
If there are multiple functions or methods, analyze each one individually.
ALWAYS format your Big O notations using math delimiters, such as \mathcal{O}(N) or \mathcal{O}(N \log N).
)PROMPT";
    output += '\n';

    // bigo.sh guards its fzf fallback with `[ $# -eq 0 ]`, and because
    // parse_arguments shifts only its own (function-local) positional
    // parameters, $# here is the ORIGINAL argument count — flags included.
    // Its output is stored in $@ but the loop iterates ARGS, so the fzf
    // result is discarded; running select_files only reproduces fzf's
    // stderr ("inappropriate ioctl for device" without a tty).
    if (ctx.args_count == 0) {
        (void)select_files(ctx);
    }

    std::string error;
    for (auto const& arg : positional_args(ctx)) {
        auto resolved = resolve_input_file(ctx, arg);
        if (!resolved) {
            error += std::format("prompt bigo: file not found: {}\n", arg);
            continue;
        }
        if (::access(resolved->c_str(), R_OK) != 0) {
            error += "prompt bigo: file not readable: \"" + std::string(arg) + "\"; resolved to \"" +
                     resolved->string() + "\"\n";
            continue;
        }
        output += std::format("File: {}\n\n```{}\n", resolved->filename().string(), infer_lang(*resolved));
        output += trim_context_nl(read_file(*resolved), head_lines);
        output += "\n```\n";
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_bigo(std::ostream& os) {
    os << R"EOF(Usage: prompt bigo [--head N] [FILE...]
       some-command | prompt bigo [--head N] [FILE...]

Asks the AI to calculate the Big O time and space complexity of the provided algorithms.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("bigo",
                            "Asks the AI to calculate the Big O time and space complexity of the provided "
                            "algorithms.",
                            &execute_bigo, &render_help_bigo);

} // namespace prompt::prompts
