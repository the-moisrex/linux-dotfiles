#include "prompt/prompts/explain_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_explain(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
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
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            output += "```\n";
            output += "\n";
        } else {
            error += "Warning: File not found or is not a regular file - " + std::string(ctx.args[i]) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_explain(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt explain [--head N] [FILE]...
       some-command | prompt explain [--head N] [FILE]...

Explain this clearly and concretely.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
