#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include "tse_data.hpp"
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

static void render_help_tse_codal(std::ostream& os);

namespace {

struct tse_codal_prompt_config {
    std::optional<std::size_t> head_lines;
    std::string symbol;
    bool fund = false;
    bool refresh = false;
    std::string top;
    bool failed = false;
    int exit_code = 2;
    std::string error;
};

tse_codal_prompt_config parse_tse_codal_args(prompt_context const& ctx) {
    tse_codal_prompt_config config;
    std::string const pfx = "prompt tse.codal: ";

    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, config.head_lines); !msg.empty()) {
        config.failed = true;
        config.error = std::move(msg);
        return config;
    }

    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        std::string_view arg = ctx.args[i];
        if (arg == "--head") {
            ++i; // value consumed by parse_head_option
            continue;
        }
        if (arg == "--top") {
            if (i + 1 >= ctx.args_count || !tse_data::is_positive_int(ctx.args[i + 1])) {
                config.failed = true;
                config.error = pfx + "--top requires a positive integer\n";
                return config;
            }
            if (ctx.args[i + 1].size() > 2) {
                config.failed = true;
                config.error = pfx + "--top must be between 1 and 20\n";
                return config;
            }
            int const top = std::stoi(std::string(ctx.args[i + 1]));
            if (top < 1 || top > 20) {
                config.failed = true;
                config.error = pfx + "--top must be between 1 and 20\n";
                return config;
            }
            config.top = std::string(ctx.args[i + 1]);
            ++i;
            continue;
        }
        if (arg == "--fund") {
            config.fund = true;
            continue;
        }
        if (arg == "--refresh") {
            config.refresh = true;
            continue;
        }
        if (arg == "--") continue;
        if (arg.starts_with('-')) {
            config.failed = true;
            config.error = pfx + "unknown option: " + std::string(arg) + "\n";
            return config;
        }
        if (!config.symbol.empty()) {
            config.failed = true;
            config.error = pfx + "provide exactly one symbol or identifier\n";
            return config;
        }
        config.symbol = std::string(arg);
    }

    if (config.symbol.empty()) {
        if (auto id = tse_data::chain_identifier(ctx)) config.symbol = *id;
        if (config.symbol.empty()) {
            if (auto id = clipboard_identifier(ctx)) config.symbol = *id;
        }
        if (config.symbol.empty()) {
            config.failed = true;
            config.error = pfx + "no symbol given and no identifier found in the chain or clipboard\n";
            config.error += help_text(render_help_tse_codal);
            return config;
        }
    }

    return config;
}

} // namespace

// Raw Codal dataset from `bin/tse.codal`, without analysis instructions — so it
// can be piped or chained into any workflow. Codal is opt-in: `prompt tse` no
// longer fetches it. The analysis lens is the data-free `tse.analyze`.
static void execute_tse_codal(prompt_context& ctx) {
    auto config = parse_tse_codal_args(ctx);
    if (config.failed) {
        throw prompt_error{config.exit_code, std::move(config.error), ""};
    }

    // The prompt and the tool share a name on purpose: `prompt tse.codal`
    // embeds exactly the Markdown `tse.codal snapshot --markdown` prints. Its
    // stderr is inherited and a failure aborts the prompt.
    std::vector<std::string> tool_argv = {
        "python3",    prompt::fs::bin_tool("tse.codal", ctx.exe_path, std::nullopt).string(), "snapshot", config.symbol,
        "--markdown",
    };
    if (!config.top.empty()) {
        tool_argv.emplace_back("--top");
        tool_argv.push_back(config.top);
    }
    if (config.fund) tool_argv.emplace_back("--fund");
    if (config.refresh) tool_argv.emplace_back("--refresh");

    // Codal's own retry deadline is 60 s (`CODAL_DEADLINE`), so this cap must
    // sit above it; the old 30 s prompt default cut a live fetch off silently.
    auto fetch = tse_data::run_tool("tse.codal", "bin/tse.codal", tool_argv, std::chrono::seconds(120));

    ctx.save_hint = tse_data::save_stem(config.symbol);
    ctx.out = trim_context(fetch.stdout_data, config.head_lines);
}

static void render_help_tse_codal(std::ostream& os) {
    os << R"EOF(Usage: prompt tse.codal [<symbol|finglish|ISIN|insCode|URL>] [--top N] [--fund] [--refresh] [--head N]

Fetch the raw Codal.ir dataset for an Iranian بورس instrument — recent announcements, extracted income-statement and balance-sheet rows, monthly portfolio reports for funds, and ratios derived from those statement rows — as Markdown, with no analysis instructions attached. Codal is opt-in: `prompt tse` fetches market data only, so chain `.tse.codal` when the filings matter (it borrows a neighbouring `.tse`/`.tse.intraday` symbol unless given its own) and then `.tse.analyze` for the institutional analysis. Accepts a Persian symbol, a finglish name (`foolad`, `khodro`), a fuzzy/company-name fragment, an ISIN (e.g. IRT1DARA0001), a TSETMC insCode, an easytrader/tsetmc/codal URL, or a composite `فولاد-IRO1FOLD0001` token — all resolved through `tse.resolve`; with no argument the other `.tse*` data prompts in the chain are searched, then the clipboard.

An Iranian IP is required. Codal results are cached for six hours; `--refresh` forces a live fetch.

Examples:
  prompt tse.codal فولاد
  prompt tse.codal foolad
  prompt tse.codal IRT1DARA0001
  prompt tse.codal فولاد --top 10
  prompt .tse فولاد .tse.codal .tse.analyze
  prompt .tse.codal فولاد --refresh

Options:
  --top N          Number of recent announcements (1-20; default: 5)
  --fund           Force monthly portfolio reports (auto-detected for funds/ETFs)
  --refresh        Ignore the Codal cache and fetch live data
  --head N         Limit lines of collected context
  -h, --help       Show this help

The data comes from `tse.codal snapshot`; run that tool directly for `search`,
`statements`, the cache controls or JSON output.
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "tse.codal",
    "Fetch the raw Codal.ir dataset for an Iranian بورس instrument — recent announcements, extracted statement "
    "rows, monthly fund portfolio reports and derived ratios — as Markdown, with no analysis instructions attached. "
    "Codal is opt-in: chain `.tse.codal` after `tse` (it borrows `tse`'s symbol), then `.tse.analyze` for the "
    "institutional "
    "analysis. Accepts a Persian symbol, a finglish name (`foolad`, `khodro`), a fuzzy/company-name fragment, an "
    "ISIN (e.g. IRT1DARA0001), a TSETMC insCode, an easytrader/tsetmc/codal URL, or a composite "
    "`فولاد-IRO1FOLD0001` token — all resolved through `tse.resolve`; with no argument the other `.tse*` data "
    "prompts in the chain are searched, then the clipboard.",
    &execute_tse_codal, &render_help_tse_codal, &tse_data::complete_instrument);

} // namespace prompt::prompts
