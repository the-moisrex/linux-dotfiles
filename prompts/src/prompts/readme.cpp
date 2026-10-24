#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_readme(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(You are a technical documentation expert.
Generate or improve a comprehensive README.md based on the provided code and context.

The README should include:
1. Project name and a concise one-line description
2. Features and key capabilities
3. Installation instructions (copy-pasteable)
4. Quick start / usage examples
5. Configuration options (if any)
6. API reference (for libraries)
7. Contributing guidelines (if applicable)
8. License

Guidelines:
- Write for someone who has never seen this project before
- Use concrete examples, not abstract descriptions
- Keep it scannable: headers, lists, code blocks
- Do not invent features that are not present in the code
- Match the tone to the project (professional for libraries, friendly for tools)

)PROMPT";

    // readme.sh: `git rev-parse --is-inside-work-tree` gates the project
    // structure block; the listing is the locale-collated `ls -1 <root> |
    // head -30` the script runs, so run it verbatim.
    if (!v.env.git_root.empty()) {
        output += "Project structure:\n\n```text\n";
        std::string root = v.env.git_root.string();
        std::vector<std::string> storage = {"bash", "-c", "ls -1 \"$1\" 2>/dev/null | head -30"};
        storage.push_back(root); // $0
        storage.push_back(root); // $1
        output += prompt::process::run_command(storage).stdout_data;
        output += "```\n\n";
    }

    std::string error;
    auto sources = collect_file_args(v);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        // readme.sh leaves a blank line before the closing fence.
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_readme(std::ostream& os) {
    os << file_usage_help("readme", "[FILE...]", "echo \"project description\"") << R"EOF(
Generates or improves a README.md for the provided code or project description.
If code files are provided, analyzes them to generate accurate documentation.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("readme",
                            "Generates or improves a README.md for the provided code or project description. If code "
                            "files are provided, analyzes them to generate accurate documentation.",
                            &execute_readme, &render_help_readme);

} // namespace prompt::prompts
