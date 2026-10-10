#include "prompt/prompts/intraday_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/tse/tse_python.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>

namespace prompt::prompts {

namespace {

struct basic_intraday_prompt_config {
    std::size_t head_lines = 0;
    std::string symbol;
};

basic_intraday_prompt_config parse_intraday_args(std::span<std::string_view const> args, prompt_context const& ctx) noexcept {
    basic_intraday_prompt_config config;
    
    std::vector<std::string_view> positional;
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto arg = args[i];
        if (arg == "--head" && i + 1 < args.size()) {
            config.head_lines = std::stoull(std::string(args[++i]));
        } else if (!arg.starts_with('-')) {
            positional.push_back(arg);
        }
    }
    
    if (!positional.empty()) {
        config.symbol = std::string(positional[0]);
    } else if (auto id = ctx.clipboard_identifier()) {
        config.symbol = *id;
    }
    
    return config;
}

} // namespace

prompt_result execute_intraday(prompt_context&& ctx) noexcept {
    auto config = parse_intraday_args({ctx.args.data(), ctx.args_count}, ctx);
    
    if (config.symbol.empty()) {
        return {"prompt intraday: no symbol given and no identifier found in the clipboard\n", 2, false};
    }
    
    auto result = tse::collect(config.symbol, 0, 5, true, true);
    
    if (!result) {
        return {"prompt intraday: " + result.error() + "\n", 2, false};
    }
    
    std::string output;
    output += R"EOF(You are a day-trader on the Tehran Stock Exchange. Analyze the instrument for a SAME-DAY trade decision.

Output exactly one of:
- LONG: [entry_price] [stop_loss] [target_1] [target_2] [position_size_pct]
- NO-TRADE: [reason]

Rules:
- Use ONLY the fetched data (quote, order_book, client_type, history, market_context)
- Entry must be near current price (last_rial or closing_rial)
- Stop loss from recent unadjusted swing low (1-2% buffer)
- Targets from resistance levels, order book walls, or risk:reward (min 1:2)
- Position size: max 5% of capital, adjusted for volatility (ATR)
- If criteria not met, output NO-TRADE with specific reason (low volume, bad risk:reward, news pending, etc.)
- Cite specific numbers from the data for every level

---
)EOF";
    
    auto render_result = tse::render_markdown(result->raw_json);
    if (render_result) {
        output += trim_context(*render_result, config.head_lines);
    }
    
    return {std::move(output), 0, false};
}

void render_help_intraday(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt intraday [<symbol|ISIN|insCode|URL>] [--head N]

Same-day trade verdict (LONG or NO-TRADE with a numeric plan) from TSETMC data.
Accepts a Persian symbol, ISIN, insCode, or easytrader/tsetmc/codal URL.
With no argument, the clipboard is searched for one.

Options:
  --head N   Limit lines of collected context
  -h, --help Show this help
)EOF";
}

} // namespace prompt::prompts