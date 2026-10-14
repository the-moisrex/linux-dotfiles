#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_output execute_fix(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Find the root problem here and propose the smallest useful fix.\n";
    output += "Explain the issue briefly, mention any important assumptions, and provide the answer "
              "primarily as a git diff that can be applied directly.\n";
    output += "Prefer minimal, surgical changes over broad rewrites.\n\n";

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
            output += "\n```\n";
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    return {.text = std::move(output), .error = std::move(error), .no_clipboard = false, .stdin_consumed = false};
}

void render_help_fix(std::ostream& os) {
    os << R"EOF(Usage: prompt fix [--head N] [FILE...]
       some-command | prompt fix [--head N] [FILE...]

Find the root problem here and propose the smallest useful fix.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
