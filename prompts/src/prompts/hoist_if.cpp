#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_hoist_if(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output =
        R"PROMPT(Analyze the code below and find opportunities to move if statements outside of for/while loops.
Focus on conditions that are loop-invariant: the condition's result does not change across iterations.
For each opportunity:
  1. Explain why the condition is invariant (what makes it safe to hoist).
  2. Provide a git diff showing the refactored code.
  3. Note any caveats (readability trade-offs, cases where hoisting is invalid).
Do not hoist conditions that depend on the loop variable or mutate state inside the loop.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File '{}' not found or is not a regular file.\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_hoist_if(std::ostream& os) {
    os << file_usage_help("hoist-if") << R"EOF(
Hoist invariant if statements out of loops for performance.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("hoist-if", "Hoist invariant if statements out of loops for performance.",
                            &execute_hoist_if, &render_help_hoist_if);

} // namespace prompt::prompts
