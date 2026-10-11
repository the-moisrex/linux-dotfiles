#include "prompt/prompts/stupid_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_stupid(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Find the stupid mistakes in this code.\n";
    output += "Focus on obvious bugs, wrong assumptions, copy-paste errors, bad edge cases, "
              "misleading names, missing checks, and anything else that would make an experienced "
              "reviewer say 'well that was silly'.\n";
    output += "Be blunt but useful. List each issue with a short explanation and the smallest "
              "practical fix.\n";
    output += "At the end, suggest small git patches.\n";
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
            // stupid.sh leaves a blank line before the closing fence.
            output += "\n```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_stupid(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt stupid [--head N] [file...]
       some-command | prompt stupid [--head N] [file...]

Find the stupid mistakes in this code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
