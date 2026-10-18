#include "../sdk/prompt_registry.hpp"
#include <ostream>
#include <string>

namespace prompt::prompts {

// The TSE/IFB session schedule. Reference data with no instructions of its own,
// so it is useful standalone or chained onto a data prompt
// (`prompt tse فولاد .tse.times`).
static void execute_tse_times(prompt_context& ctx) {
    ctx.out = R"EOF(**Tehran Stock Exchange (TSE) / Iran Fara Bourse (IFB) trading hours** (IRST, UTC+03:30):

| category | pre_open | trading |
|---|---|---|
| سهام و حق تقدم سهام (Stocks & Rights) | 08:45-09:00 | 09:00-12:30 |
| صندوق‌های سهامی و مختلط (Equity/Balanced ETFs) | 08:45-09:00 | 09:00-15:00 |
| درآمد ثابت بورس (TSE Fixed Income) | 08:45-09:00 | 09:00-15:00 |
| درآمد ثابت فرابورس (IFB Fixed Income) | 08:45-09:00 | 09:00-15:00 |
| سپرده کالایی غیر طلا (Agri Commodities) | 08:45-09:00 | 09:00-15:00 |
| طلا و سکه (Gold Funds/Coins) | 11:30-12:00 | 12:00-15:00 |
| گواهی سپرده شیشه (Glass Certificates) | 08:45-09:00 | 09:00-15:00 |
| گواهی سپرده سیمان (Cement Certificates) | 12:45-13:00 | 13:00-15:00 |
| صندوق‌های بورس انرژی (Energy Funds) | 08:45-09:00 | 09:00-15:00 |
| صندوق‌های املاک (REITs) | 08:45-09:00 | 09:00-15:00 |
| اوراق تسهیلات مسکن (Housing Facility) | 08:45-09:00 | 09:00-15:00 |
| سپرده حامل‌های انرژی (Energy Carrier) | 08:45-09:00 | 09:00-15:00 |
| صرفه‌جویی انرژی (Energy Savings) | 08:45-09:00 | 09:00-15:00 |
| صندوق در صندوق (Fund of Funds) | 08:45-09:00 | 09:00-15:00 |
| صندوق درآمد ثابت (Fixed Income Funds) | 08:45-09:00 | 09:00-15:00 |

*Trading days for all categories: Sat–Wed (IRST).*
)EOF";
}

static void render_help_tse_times(std::ostream& os) {
    os << R"EOF(Usage: prompt tse.times

The pre-open and continuous-trading session times for every Tehran Stock Exchange (TSE) and Iran Fara Bourse (IFB) instrument category, plus the Sat–Wed trading week. Reference data for reasoning about trading hours; works standalone or chained onto a data prompt.

Examples:
  prompt tse.times
  prompt tse فولاد .tse.times
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "tse.times",
    "The pre-open and continuous-trading session times for every Tehran Stock Exchange (TSE) and Iran Fara Bourse "
    "(IFB) instrument category, plus the Sat–Wed trading week. Reference data for reasoning about trading hours; "
    "works standalone or chained onto a data prompt.",
    &execute_tse_times, &render_help_tse_times);

} // namespace prompt::prompts
