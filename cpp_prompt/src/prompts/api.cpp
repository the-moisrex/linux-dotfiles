#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_output execute_api(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
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
            output += embed_file_block(file, head_lines);
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    return {std::move(output), std::move(error), false, false};
}

void render_help_api(std::ostream& os) {
    os << R"EOF(Usage: prompt api [--head N] [FILE]...
       some-command | prompt api [--head N] [FILE]...

Review this API design.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
