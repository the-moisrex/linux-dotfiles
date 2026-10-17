#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_whatwg_url_specs(prompt_context& ctx) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
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
        auto result = prompt::process::run_command({tool, "--help"});
        output += "```text\n";
        output += result.stdout_data;
        output += "```\n\n";
        ctx.out = std::move(output);
        ctx.err = "Warning: No queries provided. Use 'list' to see sections or provide specific keywords.\n";
        return;
    }

    std::string joined;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (i != 0U) joined += ' ';
        joined += sections[i];
    }
    output += std::format("Querying WHATWG URL specification for: {}\n\n", joined);
    output += "```markdown\n";

    std::vector<std::string> storage;
    storage.push_back(tool.string());
    for (auto const& s : sections) storage.push_back(s);
    auto result = prompt::process::run_command(storage);
    output += trim_context_nl(result.stdout_data, head_lines);
    output += "```\n\n";

    ctx.out = std::move(output);
}

static void render_help_whatwg_url_specs(std::ostream& os) {
    os << R"EOF(Usage: prompt whatwg-url-specs [--head N] [SECTIONS...]
       some-command | prompt whatwg-url-specs [--head N] [SECTIONS...]

Fetches relevant sections of the WHATWG URL specification using `whatwg-url-specs` and adds them to the prompt context.

Several sections can be requested in a single call:
       prompt whatwg-url-specs path-state authority-state host-state path-start-state

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("whatwg-url-specs",
                            "Fetches relevant sections of the WHATWG URL specification using `whatwg-url-specs` and "
                            "adds them to the prompt context.",
                            &execute_whatwg_url_specs, &render_help_whatwg_url_specs);

} // namespace prompt::prompts
