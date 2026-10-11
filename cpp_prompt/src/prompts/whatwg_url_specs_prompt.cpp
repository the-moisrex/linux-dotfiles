#include "prompt/prompts/whatwg_url_specs_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_whatwg_url_specs(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::vector<std::string> sections;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        sections.emplace_back(ctx.args[i]);
    }

    std::filesystem::path tool = fs::bin_tool("whatwg-url-specs", ctx.exe_path,
                                              ctx.git_root.empty() ? std::nullopt : std::optional(ctx.git_root));

    std::string output;
    output += "Review the following sections from the WHATWG URL specification.\n";
    output += "Ensure that any code, fixes, or analysis strictly adhere to these standard algorithms and "
              "definitions.\n\n";

    if (sections.empty()) {
        // No queries: the tool's --help is streamed verbatim inside a code
        // fence (no --head applied here), preceded by the stderr warning.
        std::vector<char const*> argv = {tool.c_str(), "--help", nullptr};
        auto result = prompt::process::run_command(argv);
        output += "```text\n";
        output += result.stdout_data;
        output += "```\n\n";
        return {std::move(output), 0, false,
                "Warning: No queries provided. Use 'list' to see sections or provide specific keywords.\n"};
    }

    std::string joined;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (i) joined += ' ';
        joined += sections[i];
    }
    output += "Querying WHATWG URL specification for: " + joined + "\n\n";
    output += "```markdown\n";

    std::vector<std::string> storage;
    storage.push_back(tool.string());
    for (auto const& s : sections) storage.push_back(s);
    std::vector<char const*> argv;
    argv.reserve(storage.size() + 1);
    for (auto const& s : storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    auto result = prompt::process::run_command(argv);
    output += trim_context_nl(result.stdout_data, head_lines);
    output += "```\n\n";

    return {std::move(output), 0, false, std::string{}};
}

void render_help_whatwg_url_specs(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt whatwg-url-specs [--head N] [SECTIONS...]
       some-command | prompt whatwg-url-specs [--head N] [SECTIONS...]

Fetches relevant sections of the WHATWG URL specification using `whatwg-url-specs` and adds them to the prompt context.

Several sections can be requested in a single call:
       prompt whatwg-url-specs path-state authority-state host-state path-start-state

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
