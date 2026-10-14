#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_output execute_readme(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "You are a technical documentation expert.\n";
    output += "Generate or improve a comprehensive README.md based on the provided code and "
              "context.\n";
    output += "\nThe README should include:\n1. Project name and a concise one-line description\n2. Features and key "
              "capabilities\n3. Installation instructions (copy-pasteable)\n4. Quick start / usage examples\n5. "
              "Configuration options (if any)\n6. API reference (for libraries)\n7. Contributing guidelines (if "
              "applicable)\n8. License\n\nGuidelines:\n- Write for someone who has never seen this project before\n- "
              "Use concrete examples, not abstract descriptions\n- Keep it scannable: headers, lists, code blocks\n- "
              "Do not invent features that are not present in the code\n- Match the tone to the project (professional "
              "for libraries, friendly for tools)\n\n";

    // readme.sh: `git rev-parse --is-inside-work-tree` gates the project
    // structure block; the listing is the locale-collated `ls -1 <root> |
    // head -30` the script runs, so run it verbatim.
    if (!ctx.git_root.empty()) {
        output += "Project structure:\n\n```text\n";
        std::string root = ctx.git_root.string();
        std::vector<std::string> storage = {"bash", "-c", "ls -1 \"$1\" 2>/dev/null | head -30"};
        storage.push_back(root); // $0
        storage.push_back(root); // $1
        output += prompt::process::run_command(storage).stdout_data;
        output += "```\n\n";
    }

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
            // readme.sh leaves a blank line before the closing fence.
            output += "\n```\n\n";
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    return {.text = std::move(output), .error = std::move(error), .no_clipboard = false, .stdin_consumed = false};
}

void render_help_readme(std::ostream& os) {
    os << R"EOF(Usage: prompt readme [--head N] [FILE...]
       echo "project description" | prompt readme [--head N] [FILE...]

Generates or improves a README.md for the provided code or project description.
If code files are provided, analyzes them to generate accurate documentation.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
