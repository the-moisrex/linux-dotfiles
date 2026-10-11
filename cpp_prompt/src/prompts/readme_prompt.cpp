#include "prompt/prompts/readme_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_readme(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "You are a technical documentation expert.\n";
    output += "Generate or improve a comprehensive README.md based on the provided code and "
              "context.\n";
    output += "\n";
    output += "The README should include:\n";
    output += "1. Project name and a concise one-line description\n";
    output += "2. Features and key capabilities\n";
    output += "3. Installation instructions (copy-pasteable)\n";
    output += "4. Quick start / usage examples\n";
    output += "5. Configuration options (if any)\n";
    output += "6. API reference (for libraries)\n";
    output += "7. Contributing guidelines (if applicable)\n";
    output += "8. License\n";
    output += "\n";
    output += "Guidelines:\n";
    output += "- Write for someone who has never seen this project before\n";
    output += "- Use concrete examples, not abstract descriptions\n";
    output += "- Keep it scannable: headers, lists, code blocks\n";
    output += "- Do not invent features that are not present in the code\n";
    output += "- Match the tone to the project (professional for libraries, friendly for tools)\n";
    output += "\n";

    // readme.sh: `git rev-parse --is-inside-work-tree` gates the project
    // structure block; the listing is the locale-collated `ls -1 <root> |
    // head -30` the script runs, so run it verbatim.
    if (!ctx.git_root.empty()) {
        output += "Project structure:\n";
        output += "\n";
        output += "```text\n";
        std::string root = ctx.git_root.string();
        std::vector<std::string> storage;
        storage.push_back("bash");
        storage.push_back("-c");
        storage.push_back("ls -1 \"$1\" 2>/dev/null | head -30");
        storage.push_back(root); // $0
        storage.push_back(root); // $1
        std::vector<char const*> argv;
        argv.reserve(storage.size() + 1);
        for (auto const& s : storage) argv.push_back(s.c_str());
        argv.push_back(nullptr);
        output += prompt::process::run_command(argv).stdout_data;
        output += "```\n";
        output += "\n";
    }

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
            // readme.sh leaves a blank line before the closing fence.
            output += "\n```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_readme(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt readme [--head N] [FILE...]
       echo "project description" | prompt readme [--head N] [FILE...]

Generates or improves a README.md for the provided code or project description.
If code files are provided, analyzes them to generate accurate documentation.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
