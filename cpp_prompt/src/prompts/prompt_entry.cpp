#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace prompt {

void init_prompt_registry() noexcept {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    register_prompt({"auto",
                     "Automatically chooses and executes the most appropriate prompt script based on the input. For "
                     "example, if it detects YouTube URLs, it delegates to the 'yt' prompt. If it detects C++ files, "
                     "it delegates to 'cpp-reviewer'. A bare stock identifier (ISIN, insCode, or "
                     "easytrader/tsetmc/codal URL) delegates to 'stock'. Defaults to 'summarize' for English and Farsi "
                     "text, or 'english' (translate) for other languages like Arabic.",
                     R"EOF()EOF", prompts::execute_auto, prompts::render_help_auto});

    register_prompt({"fix", "Find the root problem here and propose the smallest useful fix.", R"EOF()EOF",
                     prompts::execute_fix, prompts::render_help_fix});

    register_prompt(
        {"note", "Add a note to the prompt.", R"EOF()EOF", prompts::execute_note, prompts::render_help_note});

    register_prompt({"files",
                     "Appends the given fuzzily found files as Markdown code blocks. If you're inside a Git "
                     "repository, file headings are printed relative to the repository root. If no files are provided, "
                     "`fzf -m` is used to choose them interactively.",
                     R"EOF()EOF", prompts::execute_files, prompts::render_help_files});

    register_prompt(
        {"stock",
         "Fetch TSETMC market data, fundamentals (price ranges, average volume, fund NAV, Codal-derived ratios), "
         "market benchmarks, market context (free-market USD/IRR, Iran CPI inflation and GDP growth, large-cap "
         "breadth) and Codal financial statements (including monthly fund portfolio reports) for an Iranian بورس "
         "instrument and build a bilingual AI analysis prompt. Accepts a Persian symbol, ISIN (e.g. IRT1DARA0001), "
         "TSETMC insCode, or an easytrader/tsetmc/codal URL; with no argument the clipboard is searched for one. One "
         "or more `.stock` snapshot files (written by tse.snapshot) build the same prompt from cached data instead, "
         "warning when a snapshot is older than 24 hours.",
         R"EOF()EOF", prompts::execute_stock, prompts::render_help_stock});

    register_prompt(
        {"intraday",
         "Fetch TSETMC market data, order-book/flow snapshot, recent daily history, market context (free-market "
         "USD/IRR, Iran macro indicators, large-cap breadth) and Codal news for an Iranian بورس instrument and build "
         "an AI prompt that decides a same-day trade: buy tomorrow and sell the same day (LONG) or stand aside "
         "(NO-TRADE). Accepts a Persian symbol, ISIN (e.g. IRT1DARA0001), TSETMC insCode, or an "
         "easytrader/tsetmc/codal URL; with no argument the clipboard is searched for one.",
         R"EOF()EOF", prompts::execute_intraday, prompts::render_help_intraday});

    register_prompt(
        {"tse.find",
         "Turn a plain-language market request (\"what to buy tomorrow\", \"oversold stocks with heavy volume\", "
         "\"undervalued banks\") into a `tse.find` screening command for the Tehran Stock Exchange, or review a screen "
         "that already ran: pass a saved `.tse` snapshot, `tse.find` flags to run now, or pipe `tse.find` output "
         "(tse/json/jsonl/table) and the AI ranks the matches with the real numbers and suggests deep-dive prompts. "
         "Chat mode (default) makes the AI emit exactly one command it cannot run itself; agentic mode makes it "
         "execute the command and relax filters until the screen returns matches.",
         R"EOF()EOF", prompts::execute_tse_find, prompts::render_help_tse_find});

    register_prompt({"cpp",
                     "Builds a prompt for generating or editing C++ code. If C++ compiler errors are detected on "
                     "stdin, it automatically simplifies the prompt to focus on fixing the compilation issues.",
                     R"EOF()EOF", prompts::execute_cpp, prompts::render_help_cpp});

    register_prompt({"cpp-reviewer", "Builds a prompt for reviewing C++ code from stdin or embedded files.",
                     R"EOF()EOF", prompts::execute_cpp_reviewer, prompts::render_help_cpp_reviewer});

    register_prompt({"review", "Review this code like a strong practical reviewer.", R"EOF()EOF",
                     prompts::execute_review, prompts::render_help_review});

    register_prompt({"tests", "Review this code and identify the highest-value missing tests.", R"EOF()EOF",
                     prompts::execute_tests, prompts::render_help_tests});

    register_prompt({"refactor", "Refactor this while preserving behavior.", R"EOF()EOF", prompts::execute_refactor,
                     prompts::render_help_refactor});

    register_prompt({"run",
                     "Runs `bin/run` with the provided arguments and builds a debugging prompt from its output. If "
                     "stdin is piped in, it debugs the piped run output instead.",
                     R"EOF()EOF", prompts::execute_run, prompts::render_help_run});

    register_prompt(
        {"commit",
         "Asks the AI to write a Git commit message based on current changes. It prioritizes staged changes (git diff "
         "--cached). If no changes are staged, it evaluates all unstaged changes (git diff).",
         R"EOF()EOF", prompts::execute_commit, prompts::render_help_commit});

    register_prompt({"list", "List all available prompts with a short description.", R"EOF()EOF", prompts::execute_list,
                     prompts::render_help_list});

    register_prompt({"list-prompts",
                     "Print the full help text of every available prompt, one after another. Bash prompts are shown "
                     "via their --help output; .txt/.md prompts are printed in full.",
                     R"EOF()EOF", prompts::execute_list_prompts, prompts::render_help_list_prompts});

    register_prompt({"new",
                     "Builds a prompt that asks an AI to write a new bash prompt script following this repository's "
                     "prompt conventions.",
                     R"EOF()EOF", prompts::execute_new, prompts::render_help_new});

    register_prompt({"gtest", "Ask the AI to write Google Test (gtest) unit tests for the provided code.", R"EOF()EOF",
                     prompts::execute_gtest, prompts::render_help_gtest});

    register_prompt({"gtest-case",
                     "Builds a debugging prompt and embeds the original source for Google Test cases. By default, test "
                     "names are prefix matches.",
                     R"EOF()EOF", prompts::execute_gtest_case, prompts::render_help_gtest_case});

    register_prompt({"spp", "Builds a C++ debugging prompt and expands the given symbols through `spp`.", R"EOF()EOF",
                     prompts::execute_spp, prompts::render_help_spp});

    register_prompt({"cli",
                     "Executes the given CLI command and appends its output as a Markdown code block. Useful for "
                     "appending the output of arbitrary commands to your prompt chain.",
                     R"EOF()EOF", prompts::execute_cli, prompts::render_help_cli});

    // Natively ported prompt batches.
    register_batches();

    // list/list-prompts sort whole `name<TAB>file` lines under LC_COLLATE and
    // the bash dispatcher's file paths influenced that order (see
    // prompt_descriptor_less). All prompts are native now, so the sort key is
    // rebuilt from the directory the bash side would have used — the path is
    // only ever compared, never opened.
    auto repo = fs::find_git_root().value_or(std::filesystem::current_path());
    assign_sort_files(repo / "prompts");
}

} // namespace prompt