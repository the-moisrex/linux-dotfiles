#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

// bash: command -v <name> >/dev/null 2>&1 — mirrors whether the shell can find
// the tool on PATH.
bool tool_exists(std::string const& name) {
    std::string cmd = "command -v " + name + " >/dev/null 2>&1";
    return prompt::process::run_command({"bash", "-c", cmd}).exit_code == 0;
}

} // namespace

static void execute_cppman(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    auto pages = positional_args(ctx);

    if (!tool_exists("cppman")) {
        throw soft_error{"Error: cppman is not installed. Please install it (e.g., pip install cppman or your "
                         "package manager).\n"};
    }

    std::string output;
    output += "Below is the C++ documentation from cppreference (via cppman) for the requested topics.\n\n";

    for (auto const& page : pages) {
        output += std::format("Topic: {}\n", page);
        output += "```text\n";

        // cppman "$page" 2>/dev/null | col -bx — col's status gates the branch;
        // cppman's stderr is discarded exactly like the script.
        std::vector<std::string> storage = {"bash", "-c", "cppman \"$0\" 2>/dev/null | col -bx", page};
        auto result = prompt::process::run_command(storage);

        if (result.exit_code == 0) {
            std::string stripped = result.stdout_data;
            while (!stripped.empty() && stripped.back() == '\n') stripped.pop_back();
            if (!stripped.empty()) {
                output += trim_context_nl(result.stdout_data, head_lines);
            } else {
                output += std::format("No documentation content found for '{}'.\n", page);
            }
        } else {
            output += "Failed to fetch documentation for '" + page +
                      "'. (Are you sure it is a valid C++ standard library symbol?)\n";
        }
        output += "```\n\n";
    }

    ctx.out = std::move(output);
}

static void render_help_cppman(std::ostream& os) {
    os << R"EOF(Usage: prompt cppman [--head N] [PAGE...]
       some-command | prompt cppman [--head N] [PAGE...]

Fetches C++ documentation for the specified standard library components using `cppman`
(which pulls from cppreference.com) and appends it to the prompt context.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("cppman",
                            "Fetches C++ documentation for the specified standard library components using `cppman` "
                            "(which pulls from cppreference.com) and appends it to the prompt context.",
                            &execute_cppman, &render_help_cppman);

} // namespace prompt::prompts
