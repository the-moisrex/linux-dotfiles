#include "prompt/prompts/refactor_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_refactor(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Refactor this while preserving behavior.\n";
    output += "Focus on clarity, structure, duplication removal, naming, cohesion, and simplifying "
              "control flow without changing the intended behavior.\n";
    output += "Briefly explain the refactor plan, then provide a git diff for the recommended "
              "changes.\n";
    output += "Keep the diff as small and safe as possible.\n";
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
            output += "\n```\n\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) +
                     "' not found or is not a "
                     "regular file.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_refactor(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt refactor [--head N] [FILE...]
       some-command | prompt refactor [--head N] [FILE...]

Refactor this while preserving behavior.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
