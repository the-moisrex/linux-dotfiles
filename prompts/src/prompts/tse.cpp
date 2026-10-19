#include "../sdk/prompt_registry.hpp"
#include "tse_data.hpp"
#include <ostream>
#include <string>

namespace prompt::prompts {

static void render_help_tse(std::ostream& os);

// Raw TSETMC data for one instrument, without analysis instructions — so it
// can be piped or chained into any workflow (`prompt tse فولاد .note "compare
// with فخوز"`). The analysis prompt is a separate, data-free lens:
// `prompt .tse فولاد .tse.analyze`.
static void execute_tse(prompt_context& ctx) {
    auto config = tse_data::parse_args(ctx, "tse", &render_help_tse);
    if (config.failed) {
        throw prompt_error{config.exit_code, std::move(config.error), ""};
    }

    std::string errors;
    std::string output = tse_data::collect(ctx, config, errors);

    if (!errors.empty()) {
        ctx.err = std::move(errors);
    }
    ctx.out = std::move(output);
}

static void render_help_tse(std::ostream& os) {
    os << R"EOF(Usage: prompt tse [<symbol|finglish|ISIN|insCode|URL>] [--days N | --full] [--top N] [--no-codal] [--unadjusted] [--head N]
       prompt tse <FILE.stock>... [--head N]

Fetch the raw TSETMC dataset for an Iranian بورس instrument — market data, fundamentals (price ranges, average volume, fund NAV, Codal-derived ratios), market benchmarks, market context (free-market USD/IRR, Iran CPI inflation and GDP growth, large-cap breadth) and Codal financial statements — as Markdown, with no analysis instructions attached (append `.tse.analyze` for the institutional analysis, or `.tse.intraday` for a same-day verdict). Accepts a Persian symbol, a finglish name (`foolad`, `khodro`), a fuzzy/company-name fragment, an ISIN (e.g. IRT1DARA0001), a TSETMC insCode, an easytrader/tsetmc/codal URL, or a composite `فولاد-IRO1FOLD0001` token — all resolved through `tse.resolve`; with no argument the clipboard is searched for one. One or more `.stock` snapshot files (written by tse.snapshot) are rendered from cache instead, warning when a snapshot is older than 24 hours.

An Iranian IP is required for the public data sources. Prices are in rial.

Examples:
  prompt tse فولاد
  prompt tse foolad
  prompt tse IRT1DARA0001
  prompt tse فولاد --days 30
  prompt tse فولاد --no-codal
  prompt tse stocks/14050717_فولاد_IRO1FOLD0009.stock
  prompt tse فولاد .note "compare with فخوز"
  prompt .tse فولاد .tse.analyze

Options:
  --days N         Number of daily trading records (max: 365; default: full history)
  --full           Include all available daily records (default; may be a large prompt)
  --top N          Number of recent Codal announcements (default: 5, max: 20)
  --no-codal       Skip Codal announcements and financial statements
  --unadjusted     Show only unadjusted prices (default includes split/dividend-adjusted)
  --head N         Limit lines of collected context
  -h, --help   Show this help

Snapshot files are used as-is: --days/--full/--top/--no-codal/--unadjusted apply to
live fetches only and are rejected together with FILE arguments.
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "tse",
    "Fetch the raw TSETMC dataset for an Iranian بورس instrument — market data, fundamentals (price ranges, "
    "average volume, fund NAV, Codal-derived ratios), market benchmarks, market context (free-market USD/IRR, "
    "Iran CPI inflation and GDP growth, large-cap breadth) and Codal financial statements — as Markdown, with no "
    "analysis instructions attached (append `.tse.analyze` for the analytical prompt, or `.tse.intraday` for a "
    "same-day verdict). "
    "Accepts a Persian symbol, a finglish name (`foolad`, `khodro`), a fuzzy/company-name fragment, an ISIN "
    "(e.g. IRT1DARA0001), a TSETMC insCode, an easytrader/tsetmc/codal URL, or a composite "
    "`فولاد-IRO1FOLD0001` token — all resolved through `tse.resolve`; with "
    "no argument the clipboard is searched for one. One or more `.stock` snapshot files (written by tse.snapshot) "
    "are rendered from cache instead, warning when a snapshot is older than 24 hours.",
    &execute_tse, &render_help_tse, &tse_data::complete_instrument);

} // namespace prompt::prompts
