#include "prompt/prompts/api_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_api(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Review this API design.\n";
    output += "Look for confusing names, inconsistent behavior, unclear contracts, weak validation, "
              "awkward call sites, leaky abstractions, and backward-compatibility risks.\n";
    output += "Suggest the smallest meaningful API improvements, explain the tradeoffs briefly, and "
              "provide a git diff for the recommended changes.\n";
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
            output += "```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_api(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt api [--head N] [FILE]...
       some-command | prompt api [--head N] [FILE]...

Review this API design.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
