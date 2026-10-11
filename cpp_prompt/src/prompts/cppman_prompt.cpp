#include "prompt/prompts/cppman_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

// bash: command -v <name> >/dev/null 2>&1 — mirrors whether the shell can find
// the tool on PATH.
bool tool_exists(std::string const& name) noexcept {
    std::string cmd = "command -v " + name + " >/dev/null 2>&1";
    std::vector<char const*> argv = {"bash", "-c", cmd.c_str(), nullptr};
    return prompt::process::run_command(argv).exit_code == 0;
}

} // namespace

prompt_result execute_cppman(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::vector<std::string> pages;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        pages.emplace_back(ctx.args[i]);
    }

    if (!tool_exists("cppman")) {
        return {std::string{}, 1, false,
                "Error: cppman is not installed. Please install it (e.g., pip install cppman or your "
                "package manager).\n"};
    }

    std::string output;
    output += "Below is the C++ documentation from cppreference (via cppman) for the requested topics.\n\n";

    for (auto const& page : pages) {
        output += "Topic: " + page + "\n";
        output += "```text\n";

        // cppman "$page" 2>/dev/null | col -bx — col's status gates the branch;
        // cppman's stderr is discarded exactly like the script.
        std::vector<std::string> storage;
        storage.push_back("bash");
        storage.push_back("-c");
        storage.push_back("cppman \"$0\" 2>/dev/null | col -bx");
        storage.push_back(page);
        std::vector<char const*> argv;
        argv.reserve(storage.size() + 1);
        for (auto const& s : storage) argv.push_back(s.c_str());
        argv.push_back(nullptr);
        auto result = prompt::process::run_command(argv);

        if (result.exit_code == 0) {
            std::string stripped = result.stdout_data;
            while (!stripped.empty() && stripped.back() == '\n') stripped.pop_back();
            if (!stripped.empty()) {
                output += trim_context_nl(result.stdout_data, head_lines);
            } else {
                output += "No documentation content found for '" + page + "'.\n";
            }
        } else {
            output += "Failed to fetch documentation for '" + page +
                      "'. (Are you sure it is a valid C++ standard library symbol?)\n";
        }
        output += "```\n\n";
    }

    return {std::move(output), 0, false, std::string{}};
}

void render_help_cppman(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt cppman [--head N] [PAGE...]
       some-command | prompt cppman [--head N] [PAGE...]

Fetches C++ documentation for the specified standard library components using `cppman`
(which pulls from cppreference.com) and appends it to the prompt context.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
