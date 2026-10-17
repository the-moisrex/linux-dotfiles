#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_comments(prompt_context& ctx) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Improve the comments and inline documentation here.\n";
    output += "Add only high-value comments, docstrings, or usage notes where they genuinely help "
              "understanding.\n";
    output += "Avoid noisy commentary. Explain tricky behavior, assumptions, contracts, and "
              "non-obvious reasoning.\n";
    output += "Provide the result as a git diff.\n\n";

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
            error += std::format("Warning: File not found: {}\n", std::string(ctx.args[i]));
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_comments(std::ostream& os) {
    os << R"EOF(Usage: prompt comments [--head N] [FILE...]
       some-command | prompt comments [--head N] [FILE...]

Improve the comments and inline documentation here.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("comments", "Improve the comments and inline documentation here.",
                                                &execute_comments, &render_help_comments);

} // namespace prompt::prompts
