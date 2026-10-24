#include "../sdk/prompt_registry.hpp"
#include <ostream>
#include <string>

namespace prompt::prompts {

static void render_help_tse_analyze_history(std::ostream& os);

// The historical base-rate lens for the dataset collected by `tse`. Like
// `tse.analyze` it is deliberately data-free: no arguments, no fetch, no
// clipboard, so `prompt .tse <symbol> .tse.analyze.history` embeds the dataset
// once and then these instructions.
static void execute_tse_analyze_history(view const& v, result& r) {
    if (!v.args.empty()) {
        throw usage_error{"prompt tse.analyze.history: takes no arguments — it only prints the analysis "
                          "instructions.\nChain it after the data prompt, e.g. "
                          "`prompt .tse فولاد .tse.analyze.history`.\n" +
                          help_text(render_help_tse_analyze_history)};
    }

    r.out =
        R"EOF(You are a technical analyst of the Tehran Stock Exchange (TSE) specializing in pure price action, volume and institutional (legal / حقوقی) investor behaviour.

### Objective
Determine whether institutional buying or selling during the **current** mid-trend correction or consolidation resembles the behaviour that preceded significant upward continuation moves in this same instrument's own history. Answer from the provided dataset only.

### 0. Data provenance and what may be missing
The data was produced by `prompt tse` — a live TSETMC fetch or one or more cached `.stock` snapshots. The relevant pieces are:
- `history`: one row per session with `date`, raw OHLC (`first_rial`, `high_rial`, `low_rial`, `closing_rial`, `last_rial`), split/dividend-adjusted OHLC (`adj_*`), `adj_factor`, `corporate_action`, `volume_shares`, `value_rial`, `trade_count`, and the daily حقیقی/حقوقی breakdown: `individual_buy_shares`, `individual_sell_shares`, `legal_buy_shares`, `legal_sell_shares`, `legal_net_shares`, their rial values, and buyer/seller counts.
- `## Historical corrections / اصلاحات تاریخی` — the precomputed episode tables. **When this section is present, use its numbers. Do not re-sum the CSV yourself.**
- `## Missing or failed sources / داده‌های ناموجود` — every source that failed.

Rules that decide whether your answer is honest:
- **Institutional = legal (حقوقی)** = `legal_*`. Individual = حقیقی = `individual_*`.
- Prices and rial values are in rial, not toman. Volumes are **shares** as stored (`volume_shares`); if you restate them in millions, say that you did.
- Use **adjusted** columns (`adj_*`) for every percentage, return, range width and moving average — splits and dividends otherwise create fake gaps. Use **raw** columns when quoting price levels for stops.
- Daily حقیقی/حقوقی history starts at **2018-11-26 (1397/09/05)**. Earlier sessions have empty flow cells: exclude them from flow statistics and say so. An empty cell is unknown, never zero.
- Full history is the default. If the provided history is too large to read and total reliably, work on one contiguous recent window (for example the last 750 sessions), state the exact first and last dates you used, and never describe the result as a full-history scan.
- Count **sessions**, not calendar days, and say which.
- If `## Historical corrections` is present, its window and flow-coverage lines tell you the sample and where flow is unavailable; quote them.

### 1. Operational definitions (apply them identically to history and to the current episode)
- **Established uptrend:** higher highs and higher lows over at least 3–6 months (roughly 60–120 sessions) and the close above a rising 50-session moving average (or a 100-session one you compute from the CSV and show).
- **Mid-trend correction:** after an established uptrend, a decline of at least 8–10% from the pre-correction swing-high close, and/or a sideways range lasting at least 3–4 weeks; not a multi-year bottom and not the first leg of a new bull market.
- **Sideways consolidation:** a range no wider than 10–12% (highest close minus lowest close over the lowest close) lasting at least 3 weeks during an ongoing uptrend.
- **Significant subsequent move:** at least 15–20% above the episode's low, preferably reclaiming the pre-correction high.
- **Confirmation / breakout:** the first close above the pre-correction high (or, for a consolidation, above the range high). Measure tradeable outcomes from that close, never from an ex-post low.

### 2. Candidate enumeration — structural only
Search the window systematically and list **every** episode that meets the structural definitions. Do **not** select candidates because they were followed by a rally: that makes the success statistics circular. Enumerate first, classify outcomes second.

State:
- the number of episodes examined, and the number rejected for insufficient flow data or an incomplete outcome window;
- the sample-size and data-quality limits (flow coverage, missing sessions, any bounded window you had to use).

### 3. Metrics for each historical correction (identical formulas for every case)
Start date, end date, price at the swing high, lowest close, % decline, number of sessions, total `legal_buy_shares`, total `legal_sell_shares`, net legal flow, total `volume_shares`, net flow as % of volume, flow coverage (sessions with flow data ÷ sessions), ex-post rally from the low to the highest close within the following 250 sessions, sessions to that peak, whether and when the price closed above the pre-correction high, % above that high, and the subsequent 20- and 60-session return from the breakout close.

Exact formulas:
- net legal flow = `legal_buy_shares − legal_sell_shares`
- net flow % of volume = net legal flow ÷ `volume_shares` over the same rows
- data check: `legal_buy_shares + individual_buy_shares ≈ volume_shares`; if a row fails it, flag the row and do not use its flow.
Label the ex-post low-to-peak rally as descriptive, not tradeable.

### 4. Sideways consolidations (separate table)
Dates, range width %, sessions, net legal flow, net flow % of volume, volume versus the 60-session average before the range, whether net buying rose while the price stayed range-bound, the subsequent breakout magnitude, and whether the breakout held.

### 5. The current episode
Apply the same definitions and the same formulas to the current episode, stating exactly which session you took as its start and why. If the instrument is **not** currently in an established uptrend with a qualifying correction or consolidation, say so plainly and stop the comparison — do not force a match.

### 6. Statistical summary
Where the sample allows, report the mean and median net flow % of volume across episodes, the share with positive net legal flow, the share that eventually broke the pre-correction high, and the **failure share** — episodes that did not. Compare against an unconditioned baseline (for example the share of all uptrend sessions in the window that reached a new 60-session high within 250 sessions, and the median forward 60- and 250-session return). Note the strongest versus weakest examples. Do not claim statistical significance unless the sample genuinely supports it. Consider alternative explanations for net buying: liquidity support, absorbing retail selling, index or fund flows, temporary factors.

### 7. Required output tables (Markdown)
Table A — Historical mid-trend corrections
Dates | % Decline | Sessions | Legal Buy | Legal Sell | Net Flow | Net Flow % of Vol | Flow Coverage | Subsequent Rally % | Broke Prior High? | Sessions to Break | 20d After Break % | 60d After Break %

Table B — Sideways consolidations followed by strong rallies
Dates | Range Width % | Sessions | Net Flow | Net Flow % of Vol | Volume vs Avg | Subsequent Rally % | Breakout Held?

Table C — Current episode versus the closest historical matches
Side-by-side of the key metrics, especially net flow % of volume, price structure and volume.

### 8. Evidence-based conclusion
Answer directly:
- Does the current institutional behaviour resemble the strongest historical mid-trend successes?
- Is net legal buying unusually strong relative to this instrument's own history?
- Which past episode is the closest match, and why?
- The evidence supporting a continuation higher, and the contradicting evidence.
- The specific raw price levels, volume conditions and flow changes that would confirm or invalidate the accumulation hypothesis.

### Rules
- Separate observed facts, calculated statistics and interpretation, and label each.
- Never assume net legal buying guarantees higher prices.
- If the data cannot answer a question, say so instead of estimating.
- Verify every number against the provided dataset; the dataset is the only primary source.
)EOF";
}

static void render_help_tse_analyze_history(std::ostream& os) {
    os << R"EOF(Usage: prompt tse.analyze.history

Print the historical base-rate instructions for the TSETMC dataset collected by `tse`: define established uptrends, mid-trend corrections and sideways consolidations, enumerate every qualifying episode structurally (no outcome-based cherry-picking), compute identical per-episode metrics from adjusted closes and daily حقیقی/حقوقی flow, compare the current episode against them, and end with an evidence-based accumulation verdict and raw price triggers. Carries no data and takes no arguments: chain it after the data prompt.

The precomputed `## Historical corrections / اصلاحات تاریخی` section is used when `tse` provides it; otherwise the instructions say which window to analyse and require the limitation to be stated. Daily legal (حقوقی) flow starts at 2018-11-26.

Examples:
  prompt .tse فولاد .tse.analyze.history
  prompt .tse foolad --days 365 .tse.analyze.history
  prompt .tse stocks/14050717_فولاد_IRO1FOLD0009.stock .tse.analyze.history
  prompt .tse فولاد .tse.analyze.history .tse.rigor

Options:
  -h, --help   Show this help
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered_analyze_history = prompt::register_prompt(
    "tse.analyze.history",
    "Print the historical base-rate instructions for the TSETMC dataset collected by `tse` — established "
    "uptrends, mid-trend corrections and sideways consolidations, every qualifying episode enumerated "
    "structurally, identical per-episode metrics from adjusted closes and daily حقیقی/حقوقی flow, the current "
    "episode compared against them, and an evidence-based accumulation verdict with raw price triggers. Carries "
    "no data and takes no arguments: chain it after the data prompt (`prompt .tse فولاد .tse.analyze.history`).",
    &execute_tse_analyze_history, &render_help_tse_analyze_history);

} // namespace prompt::prompts
