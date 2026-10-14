#include "prompt_entry.hpp"
#include "../core/fs.hpp"
#include "../sdk/prompt_registry.hpp"
#include "prompts.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace prompt {

void init_prompt_registry() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    register_prompt({
        .name = "auto",
        .help_summary =
            "Automatically chooses and executes the most appropriate prompt script based on the input. For "
            "example, if it detects YouTube URLs, it delegates to the 'yt' prompt. If it detects C++ files, "
            "it delegates to 'cpp-reviewer'. A bare stock identifier (ISIN, insCode, or "
            "easytrader/tsetmc/codal URL) delegates to 'stock'. Defaults to 'summarize' for English and Farsi "
            "text, or 'english' (translate) for other languages like Arabic.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_auto,
        .render_help_fn = prompts::render_help_auto,
    });

    register_prompt({
        .name = "fix",
        .help_summary = "Find the root problem here and propose the smallest useful fix.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_fix,
        .render_help_fn = prompts::render_help_fix,
    });

    register_prompt({
        .name = "note",
        .help_summary = "Add a note to the prompt.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_note,
        .render_help_fn = prompts::render_help_note,
    });

    register_prompt({
        .name = "files",
        .help_summary =
            "Appends the given fuzzily found files as Markdown code blocks. If you're inside a Git "
            "repository, file headings are printed relative to the repository root. If no files are provided, "
            "`fzf -m` is used to choose them interactively.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_files,
        .render_help_fn = prompts::render_help_files,
    });

    register_prompt({
        .name = "stock",
        .help_summary =
            "Fetch TSETMC market data, fundamentals (price ranges, average volume, fund NAV, Codal-derived ratios), "
            "market benchmarks, market context (free-market USD/IRR, Iran CPI inflation and GDP growth, large-cap "
            "breadth) and Codal financial statements (including monthly fund portfolio reports) for an Iranian بورس "
            "instrument and build a bilingual AI analysis prompt. Accepts a Persian symbol, ISIN (e.g. IRT1DARA0001), "
            "TSETMC insCode, or an easytrader/tsetmc/codal URL; with no argument the clipboard is searched for one. "
            "One "
            "or more `.stock` snapshot files (written by tse.snapshot) build the same prompt from cached data instead, "
            "warning when a snapshot is older than 24 hours.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_stock,
        .render_help_fn = prompts::render_help_stock,
    });

    register_prompt({
        .name = "intraday",
        .help_summary =
            "Fetch TSETMC market data, order-book/flow snapshot, recent daily history, market context (free-market "
            "USD/IRR, Iran macro indicators, large-cap breadth) and Codal news for an Iranian بورس instrument and "
            "build "
            "an AI prompt that decides a same-day trade: buy tomorrow and sell the same day (LONG) or stand aside "
            "(NO-TRADE). Accepts a Persian symbol, ISIN (e.g. IRT1DARA0001), TSETMC insCode, or an "
            "easytrader/tsetmc/codal URL; with no argument the clipboard is searched for one.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_intraday,
        .render_help_fn = prompts::render_help_intraday,
    });

    register_prompt({
        .name = "tse.find",
        .help_summary =
            "Turn a plain-language market request (\"what to buy tomorrow\", \"oversold stocks with heavy volume\", "
            "\"undervalued banks\") into a `tse.find` screening command for the Tehran Stock Exchange, or review a "
            "screen "
            "that already ran: pass a saved `.tse` snapshot, `tse.find` flags to run now, or pipe `tse.find` output "
            "(tse/json/jsonl/table) and the AI ranks the matches with the real numbers and suggests deep-dive prompts. "
            "Chat mode (default) makes the AI emit exactly one command it cannot run itself; agentic mode makes it "
            "execute the command and relax filters until the screen returns matches.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_tse_find,
        .render_help_fn = prompts::render_help_tse_find,
    });

    register_prompt({
        .name = "cpp",
        .help_summary = "Builds a prompt for generating or editing C++ code. If C++ compiler errors are detected on "
                        "stdin, it automatically simplifies the prompt to focus on fixing the compilation issues.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_cpp,
        .render_help_fn = prompts::render_help_cpp,
    });

    register_prompt({
        .name = "cpp-reviewer",
        .help_summary = "Builds a prompt for reviewing C++ code from stdin or embedded files.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_cpp_reviewer,
        .render_help_fn = prompts::render_help_cpp_reviewer,
    });

    register_prompt({
        .name = "review",
        .help_summary = "Review this code like a strong practical reviewer.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_review,
        .render_help_fn = prompts::render_help_review,
    });

    register_prompt({
        .name = "tests",
        .help_summary = "Review this code and identify the highest-value missing tests.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_tests,
        .render_help_fn = prompts::render_help_tests,
    });

    register_prompt({
        .name = "refactor",
        .help_summary = "Refactor this while preserving behavior.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_refactor,
        .render_help_fn = prompts::render_help_refactor,
    });

    register_prompt({
        .name = "run",
        .help_summary = "Runs `bin/run` with the provided arguments and builds a debugging prompt from its output. If "
                        "stdin is piped in, it debugs the piped run output instead.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_run,
        .render_help_fn = prompts::render_help_run,
    });

    register_prompt({
        .name = "commit",
        .help_summary = "Asks the AI to write a Git commit message based on current changes. It prioritizes staged "
                        "changes (git diff "
                        "--cached). If no changes are staged, it evaluates all unstaged changes (git diff).",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_commit,
        .render_help_fn = prompts::render_help_commit,
    });

    register_prompt({
        .name = "list",
        .help_summary = "List all available prompts with a short description.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_list,
        .render_help_fn = prompts::render_help_list,
    });

    register_prompt({
        .name = "list-prompts",
        .help_summary = "Print the full help text of every available prompt, one after another. Bash prompts are shown "
                        "via their --help output; .txt/.md prompts are printed in full.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_list_prompts,
        .render_help_fn = prompts::render_help_list_prompts,
    });

    register_prompt({
        .name = "new",
        .help_summary = "Builds a prompt that asks an AI to write a new bash prompt script following this repository's "
                        "prompt conventions.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_new,
        .render_help_fn = prompts::render_help_new,
    });

    register_prompt({
        .name = "gtest",
        .help_summary = "Ask the AI to write Google Test (gtest) unit tests for the provided code.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_gtest,
        .render_help_fn = prompts::render_help_gtest,
    });

    register_prompt({
        .name = "gtest-case",
        .help_summary =
            "Builds a debugging prompt and embeds the original source for Google Test cases. By default, test "
            "names are prefix matches.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_gtest_case,
        .render_help_fn = prompts::render_help_gtest_case,
    });

    register_prompt({
        .name = "spp",
        .help_summary = "Builds a C++ debugging prompt and expands the given symbols through `spp`.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_spp,
        .render_help_fn = prompts::render_help_spp,
    });

    register_prompt({
        .name = "cli",
        .help_summary = "Executes the given CLI command and appends its output as a Markdown code block. Useful for "
                        "appending the output of arbitrary commands to your prompt chain.",
        .help_full = R"EOF()EOF",
        .execute_fn = prompts::execute_cli,
        .render_help_fn = prompts::render_help_cli,
    });

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