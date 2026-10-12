#include "prompt/prompts/prompts.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_output execute_explain(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Explain this clearly and concretely.\n";
    output += "Describe what it does, how the parts fit together, key control flow, important "
              "assumptions, and likely failure points.\n";
    output += "If useful, finish with a small git diff that improves readability through naming, "
              "structure, or comments.\n";
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
            error += std::format("Warning: File not found or is not a regular file - {}\n", std::string(ctx.args[i]));
        }
    }

    return {std::move(output), std::move(error), false, false};
}

void render_help_explain(std::ostream& os) {
    os << R"EOF(Usage: prompt explain [--head N] [FILE]...
       some-command | prompt explain [--head N] [FILE]...

Explain this clearly and concretely.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
