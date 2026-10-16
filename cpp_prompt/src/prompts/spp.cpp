#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_spp(prompt_context& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    // spp.sh tests the raw $# (so `prompt spp --head 5` is not an error).
    if (ctx.args_count == 0) {
        throw usage_error{"Usage: prompt spp [--head N] <symbol> [symbol...]\n"};
    }

    auto tool = prompt::fs::bin_tool("spp", ctx.exe_path, ctx.git_root);

    std::string output;
    output += "Additional C++ symbol context:\n\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::string symbol(ctx.args[i]);
        output += std::format("Symbol: {}\n\n", symbol);
        output += "```cpp\n";

        std::string spp_path = tool.string();
        std::vector<char const*> argv = {spp_path.c_str(), symbol.c_str(), nullptr};
        auto result = prompt::process::run_command(argv, ctx.stdin_content);
        output += trim_context_nl(result.stdout_data, head_lines);
        output += "\n```\n\n";
        if (!result.stderr_data.empty()) error += result.stderr_data;
    }

    output += "Use the symbol context above when analyzing the issue.";
    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_spp(std::ostream& os) {
    os << R"EOF(Usage: prompt spp [--head N] <symbol> [symbol...]

Builds a C++ debugging prompt and expands the given symbols through `spp`.

Options:
  --head N   Keep only the first N lines of each symbol expansion
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("spp", "Builds a C++ debugging prompt and expands the given symbols through `spp`.",
                            &execute_spp, &render_help_spp);

} // namespace prompt::prompts
