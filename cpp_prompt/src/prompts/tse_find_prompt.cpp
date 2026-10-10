#include "prompt/prompts/tse_find_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/core/python.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

namespace {

struct basic_tse_find_prompt_config {
    std::size_t head_lines = 0;
    std::string query;
    bool agentic = false;
    int max_iter = 3;
    bool dry_run = false;
};

basic_tse_find_prompt_config parse_tse_find_args(std::span<std::string_view const> args) noexcept {
    basic_tse_find_prompt_config config;
    
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto arg = args[i];
        if (arg == "--head" && i + 1 < args.size()) {
            config.head_lines = std::stoull(std::string(args[++i]));
        } else if (arg == "--mode" && i + 1 < args.size()) {
            config.agentic = (args[++i] == "agentic");
        } else if (arg == "--max-iter" && i + 1 < args.size()) {
            config.max_iter = std::stoi(std::string(args[++i]));
        } else if (arg == "--dry-run") {
            config.dry_run = true;
        } else if (!arg.starts_with('-')) {
            if (config.query.empty()) config.query = std::string(arg);
            else config.query += " " + std::string(arg);
        }
    }
    
    return config;
}

} // namespace

prompt_result execute_tse_find(prompt_context&& ctx) noexcept {
    auto config = parse_tse_find_args({ctx.args.data(), ctx.args_count});
    
    std::string output;
    
    if (config.query.empty() && ctx.stdin_consumed) {
        config.query = ctx.stdin_content;
    }
    
    if (config.query.empty()) {
        return {"prompt tse.find: no query provided\n", 2, false};
    }
    
    std::filesystem::path query_path(config.query);
    if (query_path.extension() == ".tse") {
        output += "Review mode: analyzing cached tse.find results from " + query_path.string() + "\n\n";
        output += "[Review mode not yet implemented]\n";
        return {std::move(output), 0, false};
    }
    
    output += "You are a TSETMC screening expert. Convert the user's request into a `tse.find` command.\n\n";
    output += "User request: " + config.query + "\n\n";
    output += "Available tse.find flags:\n";
    output += "--price-min N --price-max N --vol-min N --vol-max N --change-min N --change-max N\n";
    output += "--pe-min N --pe-max N --market-cap-min N --market-cap-max N\n";
    output += "--industry STR --sector STR --board STR --fund-only --no-fund\n";
    output += "--sort-by price|volume|change|pe|market-cap --sort-desc\n";
    output += "--limit N --json|--table|--csv\n\n";
    
    if (config.agentic) {
        output += "Mode: AGENTIC — you may run up to " + std::to_string(config.max_iter) + " iterations.\n";
        output += "After each run, analyze results and refine the query.\n";
        if (config.dry_run) output += "DRY RUN: print commands only.\n";
    } else {
        output += "Mode: CHAT — output a single runnable tse.find command line.\n";
    }
    
    return {std::move(output), 0, false};
}

void render_help_tse_find(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt tse.find <query> [--mode agentic] [--max-iter N] [--dry-run] [--head N]

Turn a plain-language market request into a tse.find screening command.

Modes:
  chat (default)    Emit one runnable tse.find command line
  agentic           Add run-and-relax instructions (--max-iter N, --dry-run)

Options:
  --mode M          chat | agentic (default: chat)
  --max-iter N      Max iterations for agentic mode (default: 3)
  --dry-run         Print command without executing
  --head N          Limit output lines
  -h, --help        Show this help
)EOF";
}

} // namespace prompt::prompts