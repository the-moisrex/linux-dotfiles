#include "../sdk/prompt_registry.hpp"
#include <ostream>
#include <string>

namespace prompt::prompts {

static void render_help_tse_rigor(std::ostream& os);

// A standalone checklist that keeps market/TSE analysis and backtests honest.
// Chain it onto a data prompt (`prompt tse فولاد .tse.rigor`) or run it on its own.
static void execute_tse_rigor(prompt_context& ctx) {
    ctx.out = R"EOF(**When analyzing Tehran Stock Exchange (TSE) data or any market backtest:**

1. **Never compute buyer/seller power or ratios manually in prose.**
   First build a clear table (or code output) with: date, closing_rial, last_rial, volume, individual_buy_shares, individual_sell_shares, individual_buyers, individual_sellers, and the computed power. Show the exact arithmetic for any example you cite.

2. **Never cite approximate averages, moving averages, or levels** unless you compute them directly from the data and show the window/dates.

3. **Avoid look-ahead bias completely.**
   Never measure returns from an ex-post low or high of a correction. Use only realistic, no-lookahead entry rules (e.g. confirmation close, next-day open after signal).

4. **Any backtest result must include at minimum:**
   - Baseline comparison
   - Median return (not just mean)
   - Mean win / mean loss
   - Max win / max loss
   - Expectancy
   - Clear statement whether stops/costs are included

5. **Distinguish clearly between:**
   - "Not independently verifiable from the provided input"
   - "Methodologically invalid"
   - "Reported by another party, not independently reproduced"

6. **Do not shift standards or move goalposts.**
   If a methodological flaw is correctly fixed, explicitly acknowledge it. Do not keep attacking a corrected version with the old critique.

7. **Label small-sample historical examples as "illustrative, not statistical."**
   Do not treat 2–3 observations as established patterns.

8. **Always specify whether a price is `closing_rial` or `last_rial`.**

9. **If a computed number contradicts your narrative, discard the example.**
   Do not force the story.

10. **For any statistics provided by another AI or user that you cannot fully reproduce from the input, explicitly label them as "reported, not independently verified."**

11. **Clearly separate:**
    - What is statistically tested
    - What is discretionary price-action judgment

12. **Always highlight left-tail risk** (max loss, fat tails) with the same prominence as positive mean returns. If max losses are extreme, state that the edge is unusable without hard stops.
)EOF";
}

static void render_help_tse_rigor(std::ostream& os) {
    os << R"EOF(Usage: prompt tse.rigor

A verification checklist that keeps Tehran Stock Exchange (TSE) and general market analysis or backtests honest: compute ratios from a table instead of prose, never cite uncomputed averages or levels, avoid look-ahead bias, require baseline/median/win-loss/expectancy statistics, separate tested facts from discretionary judgment, and give left-tail risk the same prominence as mean returns. Works standalone or chained onto a data prompt.

Examples:
  prompt tse.rigor
  prompt tse فولاد .tse.rigor
  prompt stock فولاد .tse.rigor
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "tse.rigor",
    "A verification checklist that keeps Tehran Stock Exchange (TSE) and general market analysis or backtests "
    "honest: compute ratios from a table instead of prose, never cite uncomputed averages or levels, avoid "
    "look-ahead bias, require baseline/median/win-loss/expectancy statistics, separate tested facts from "
    "discretionary judgment, and give left-tail risk the same prominence as mean returns. Works standalone or "
    "chained onto a data prompt.",
    &execute_tse_rigor, &render_help_tse_rigor);

} // namespace prompt::prompts
