#include "prompt/prompts/comments_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_comments(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Improve the comments and inline documentation here.\n";
    output += "Add only high-value comments, docstrings, or usage notes where they genuinely help "
              "understanding.\n";
    output += "Avoid noisy commentary. Explain tricky behavior, assumptions, contracts, and "
              "non-obvious reasoning.\n";
    output += "Provide the result as a git diff.\n";
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
            output += "\n```\n";
            output += "\n";
        } else {
            error += "Warning: File not found: " + std::string(ctx.args[i]) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_comments(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt comments [--head N] [FILE...]
       some-command | prompt comments [--head N] [FILE...]

Improve the comments and inline documentation here.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
