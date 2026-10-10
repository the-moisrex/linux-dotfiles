#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/legacy/legacy_prompts.hpp"
#include "prompt/sdk/prompt_registry.hpp"

namespace prompt {

void init_prompt_registry() noexcept {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    register_prompt({"auto", "Automatically chooses the most appropriate prompt based on input.",
                     R"EOF(Usage: prompt auto [FILE...]
       some-command | prompt auto [FILE...]

Automatically chooses and executes the most appropriate prompt script based on the input.
For example, if it detects YouTube URLs, it delegates to the 'yt' prompt.
If it detects C++ files, it delegates to 'cpp-reviewer'.
A bare stock identifier delegates to 'stock'.
Defaults to 'summarize' for English and Farsi text, or 'english' (translate) for other languages.

Options:
  --help, -h   Show this help message
  (Any other options like --head are passed through to the selected script)
)EOF",
                     prompts::execute_auto, prompts::render_help_auto});

    register_prompt({"fix", "Find the root problem and propose the smallest useful fix.",
                     R"EOF(Usage: prompt fix [--head N] [FILE...]
       some-command | prompt fix [--head N] [FILE...]

Find the root problem here and propose the smallest useful fix.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_fix, prompts::render_help_fix});

    register_prompt({"note", "Add a note to the prompt (before or after context).",
                     R"EOF(Usage: prompt note [--head N] [NOTE...]
       some-command | prompt note [--head N] [NOTE...]

Add a note to the prompt.

The note is placed after the embedded context by default; use --prepend
to place it before everything.

Options:
  --head N      Keep only the first N lines of the embedded context
  --prepend|-p  Add before everything
)EOF",
                     prompts::execute_note, prompts::render_help_note});

    register_prompt({"files", "Append files as Markdown code blocks (with fzf fallback).",
                     R"EOF(Usage: prompt files [--head N] [FILE...]
       some-command | prompt files [--head N] [FILE...]

Appends the given fuzzily found files as Markdown code blocks.
If you're inside a Git repository, file headings are printed relative to the
repository root.
If no files are provided, fzf -m is used to choose them interactively.

Options:
  --head N   Keep only the first N lines of each embedded file
)EOF",
                     prompts::execute_files, prompts::render_help_files});

    register_prompt(
        {"stock", "Fetch TSETMC market data and build AI analysis prompt for Iranian stocks/funds.",
         R"EOF(Usage: prompt stock [<symbol|ISIN|insCode|URL>] [--days N | --full] [--top N] [--no-codal] [--unadjusted] [--head N]
       prompt stock <FILE.stock>... [--head N]

Fetch TSETMC market data, fundamentals, market benchmarks, market context (USD/IRR, Iran CPI/GDP, large-cap breadth) and Codal financial statements for an Iranian بورس instrument and build a bilingual AI analysis prompt.

Options:
  --days N         Number of daily trading records (max: 365; default: full history)
  --full           Include all available daily records (default; may be a large prompt)
  --top N          Number of recent Codal announcements (default: 5, max: 20)
  --no-codal       Skip Codal announcements and financial statements
  --unadjusted     Show only unadjusted prices (default includes split/dividend-adjusted)
  --head N         Limit lines of collected context
  -h, --help       Show this help
)EOF",
         prompts::execute_stock, prompts::render_help_stock});

    register_prompt({"intraday", "Same-day trade verdict (LONG or NO-TRADE) from TSETMC data.",
                     R"EOF(Usage: prompt intraday [<symbol|ISIN|insCode|URL>] [--head N]

Same-day trade verdict (LONG or NO-TRADE with a numeric plan) from TSETMC data.
Accepts a Persian symbol, ISIN, insCode, or easytrader/tsetmc/codal URL.
With no argument, the clipboard is searched for one.

Options:
  --head N   Limit lines of collected context
  -h, --help Show this help
)EOF",
                     prompts::execute_intraday, prompts::render_help_intraday});

    register_prompt({"tse.find", "Screen TSETMC instruments from natural language query.",
                     R"EOF(Usage: prompt tse.find <query> [--mode agentic] [--max-iter N] [--dry-run] [--head N]

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
)EOF",
                     prompts::execute_tse_find, prompts::render_help_tse_find});

    register_prompt({"cpp", "Analyze C++ compiler errors and suggest fixes.",
                     R"EOF(Usage: prompt cpp [--head N] [FILE...]
       compiler-output | prompt cpp [--head N] [FILE...]

Analyze C++ compiler/linker errors and suggest fixes.
Auto-detects GCC/Clang error format from stdin.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_cpp, prompts::render_help_cpp});

    register_prompt({"cpp-reviewer", "Comprehensive C++ code review (style, performance, correctness, modern C++).",
                     R"EOF(Usage: prompt cpp-reviewer [--head N] [FILE...]
       some-command | prompt cpp-reviewer [--head N] [FILE...]

Comprehensive C++ code review covering:
- Modern C++ usage (C++20/23/26 features, RAII, algorithms)
- Performance (allocations, copies, move semantics, cache efficiency)
- Correctness (UB, lifetime, thread safety, exception safety)
- Architecture (dependencies, coupling, abstractions, templates)
- Style (naming, formatting, project conventions)

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_cpp_reviewer, prompts::render_help_cpp_reviewer});

    register_prompt({"review", "General code review prompt for any language.",
                     R"EOF(Usage: prompt review [--head N] [FILE...]
       some-command | prompt review [--head N] [FILE...]

Review code for correctness, style, performance, and security issues.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_review, prompts::render_help_review});

    register_prompt({"tests", "Generate unit tests for the given code.",
                     R"EOF(Usage: prompt tests [--head N] [FILE...]
       some-command | prompt tests [--head N] [FILE...]

Generate comprehensive unit tests for the given code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_tests, prompts::render_help_tests});

    register_prompt({"refactor", "Refactor code for clarity, performance, and maintainability.",
                     R"EOF(Usage: prompt refactor [--head N] [FILE...]
       some-command | prompt refactor [--head N] [FILE...]

Refactor the given code for clarity, performance, and maintainability.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_refactor, prompts::render_help_refactor});

    register_prompt({"run", "Build and run a CMake target, embed output for debugging.",
                     R"EOF(Usage: prompt run [--head N] <target> [args...]

Find git root, locate CMake build dir, build and run target, embed output.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_run, prompts::render_help_run});

    register_prompt({"commit", "Generate Conventional Commit message from git diff.",
                     R"EOF(Usage: prompt commit [--head N]

Generate a Conventional Commit message from staged/unstaged git diff.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_commit, prompts::render_help_commit});

    register_prompt({"list", "List all available prompts with short descriptions.",
                     R"EOF(Usage: prompt list [--names]

List all available prompts with a short description.

The description is the first paragraph of the prompt's --help output,
joined into one line.

Options:
  --names        Print only prompt names, one per line (fast: skips --help)
  -h, --help     Show this help
)EOF",
                     prompts::execute_list, prompts::render_help_list});

    register_prompt({"list-prompts", "Print the full help text of every available prompt.",
                     R"EOF(Usage: prompt list-prompts

Print the full help text of every available prompt.
)EOF",
                     prompts::execute_list_prompts, prompts::render_help_list_prompts});

    register_prompt({"new", "Generate a new prompt script from a description.",
                     R"EOF(Usage: prompt new [--head N] <prompt-name> [description...]

Generate a new prompt script (C++ module) from a natural language description.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_new, prompts::render_help_new});

    register_prompt({"gtest", "Find and embed Google Test test case source code.",
                     R"EOF(Usage: prompt gtest [--head N] <TEST_NAME>...

Find Google Test test case source from names and embed for analysis.

Options:
  --head N   Keep only the first N lines of each embedded test
)EOF",
                     prompts::execute_gtest, prompts::render_help_gtest});

    register_prompt({"gtest-case", "Find and embed a single Google Test case source.",
                     R"EOF(Usage: prompt gtest-case [--head N] <TEST_NAME>

Find a single Google Test case source from its name.

Options:
  --head N   Keep only the first N lines of the embedded test
)EOF",
                     prompts::execute_gtest_case, prompts::render_help_gtest_case});

    register_prompt({"spp", "Expand C++ symbol via clang (parallel, deduplicated).",
                     R"EOF(Usage: prompt spp [--head N] <SYMBOL> [FILE...]

Extract full C++ function/class source using clang.
Reads .clang/.clangd for compile flags.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_spp, prompts::render_help_spp});

    register_prompt({"cli", "Run a shell command and embed its output.",
                     R"EOF(Usage: prompt cli [--head N] <COMMAND>

Run a shell command and embed its output for AI context.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_cli, prompts::render_help_cli});

    // Every remaining prompt file in the search directories (bash/txt/md)
    // is registered as a legacy-backed prompt so it is directly
    // addressable by name — parity with the bash dispatcher.
    legacy::register_legacy_prompts();
}

} // namespace prompt