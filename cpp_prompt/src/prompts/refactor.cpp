#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_refactor(prompt_context& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Refactor this while preserving behavior.\n";
    output += "Focus on clarity, structure, duplication removal, naming, cohesion, and simplifying "
              "control flow without changing the intended behavior.\n";
    output += "Briefly explain the refactor plan, then provide a git diff for the recommended "
              "changes.\n";
    output += "Keep the diff as small and safe as possible.\n\n";

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
            output += "\n```\n\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) +
                     "' not found or is not a "
                     "regular file.\n";
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_refactor(std::ostream& os) {
    os << R"EOF(Usage: prompt refactor [--head N] [FILE...]
       some-command | prompt refactor [--head N] [FILE...]

Refactor this while preserving behavior.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("refactor", "Refactor this while preserving behavior.",
                                                &execute_refactor, &render_help_refactor);

} // namespace prompt::prompts
