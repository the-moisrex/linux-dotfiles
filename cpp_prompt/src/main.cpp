#include "prompt/core/clipboard.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/core/python.hpp"
#include "prompt/core/sanitize.hpp"
#include "prompt/legacy/legacy_runner.hpp"
#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/sdk/chaining.hpp"
#include "prompt/sdk/pipeline.hpp"
#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

int main(int argc, char** argv) {
    // Initialize prompt registry (needed even for --help)
    prompt::init_prompt_registry();

    // Handle --help and --version early (only as first argument)
    if (argc > 1) {
        std::string_view arg(argv[1]);
        if (arg == "--help" || arg == "-h") {
            // Same usage text as the bash dispatcher's usage().
            std::cout << R"(Usage:
  prompt --help
  prompt list
  prompt list-prompts
  prompt <name> [args...]                # run a single prompt
  prompt <name> [-- arg1 arg2 ...]       # explicit end of prompt flags

Shorthand (chain multiple prompts with args):
  prompt .note "Some details" .cli "pwd" .files src/main.cpp
  prompt .agents .git-dirty .commit

  Tokens starting with '.' or '-' that match a known prompt name are treated
  as prompt invocations. Bare words between them become that prompt's arguments.
  The first argument is also checked as a prompt name when no prompts have been
  matched yet.

Examples:
  cat file.cpp | prompt cpp-debug
  prompt cpp-debug --flag value
  prompt .note "WIP: fixing parser bug" .files parser.h parser.cpp .cli "make test"
  prompt .agents .files one two three .note "Some details" .cli "pwd"

Notes:
  - Searches $XDG_CONFIG_DIRS/prompts and ../prompts (relative to this script) for prompt files.
  - Recognized extensions (in search order): .sh .txt .md then no extension.
  - For .sh prompts, the script will be executed with bash and receive STDIN and any additional arguments.
  - For .txt/.md (or no-ext treated as text) prompts the file contents are printed, then STDIN is appended (if any).
  - 'prompt list' is itself a prompt script: it lists available prompts with
    the first paragraph of each prompt's help as the description (--names for
    bare names only).
  - 'prompt list-prompts' is also a prompt script: it prints the full help
    text of every available prompt.
)";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "prompt " << PROMPT_VERSION << "\n";
            return 0;
        }
    }

    // Initialize clipboard
    prompt::clipboard::init();

    // Prepare base context
    auto base_ctx = prompt::make_base_context();

    // Wire up services
    base_ctx.clipboard_content_fn = &prompt::clipboard::paste;
    base_ctx.clipboard_identifier_fn = []() -> std::optional<std::string> {
        auto content = prompt::clipboard::paste();
        if (content.empty()) return std::nullopt;

        std::string text = content;
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());

        static const std::regex isin_re(R"(IR[A-Za-z0-9]{10})");
        static const std::regex inscode_re(R"([0-9]{15,})");
        static const std::regex easytrader_re(
            R"(https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12})");
        static const std::regex tsetmc_re(R"(https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,})");
        static const std::regex codal_re(
            R"(https?://(www\.)?codal\.ir/ReportList\.aspx\?([^&]*&)?Symbol=[^&[:space:]]+)");

        std::smatch match;
        if (std::regex_search(text, match, easytrader_re) || std::regex_search(text, match, tsetmc_re) ||
            std::regex_search(text, match, codal_re) || std::regex_search(text, match, isin_re) ||
            std::regex_search(text, match, inscode_re)) {
            return match[0];
        }

        if (!text.empty() && text.size() <= 64) {
            bool has_latin = false;
            for (char c : text) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                    has_latin = true;
                    break;
                }
            }
            if (!has_latin) return text;
        }

        return std::nullopt;
    };

    base_ctx.select_files_fn = []() -> std::vector<std::filesystem::path> {
        std::vector<char const*> fzf_argv = {"fzf", "-m", nullptr};
        auto result = prompt::process::run_command(fzf_argv);
        std::vector<std::filesystem::path> files;
        std::string line;
        for (char c : result.stdout_data) {
            if (c == '\n') {
                if (!line.empty()) {
                    files.emplace_back(line);
                    line.clear();
                }
            } else {
                line += c;
            }
        }
        if (!line.empty()) files.emplace_back(line);
        return files;
    };

    base_ctx.resolve_input_file_fn = [](std::string_view name) -> std::optional<std::filesystem::path> {
        std::filesystem::path p(name);
        if (std::filesystem::is_regular_file(p)) return p;

        auto git_root = prompt::fs::find_git_root();
        if (git_root) {
            auto full = *git_root / name;
            if (std::filesystem::is_regular_file(full)) return full;
        }

        // _common.sh resolve_input_file: filter the repo's files with fzf.
        // stderr is silenced (fzf complains without a tty) because most
        // callers capture the function through a command substitution.
        std::string name_str(name);
        std::vector<std::string> storage;
        storage.push_back("bash");
        storage.push_back("-c");
        if (git_root) {
            storage.push_back("cd \"$0\" && git ls-files --cached --others --exclude-standard 2>/dev/null | "
                              "fzf -f \"$1\" 2>/dev/null | head -n 1");
            storage.push_back(git_root->string());
        } else {
            storage.push_back("rg --files 2>/dev/null || find . -type f | fzf -f \"$0\" 2>/dev/null | head -n 1");
        }
        storage.push_back(name_str);

        std::vector<char const*> argv;
        argv.reserve(storage.size() + 1);
        for (auto const& s : storage) argv.push_back(s.c_str());
        argv.push_back(nullptr);

        auto result = prompt::process::run_command(argv, "");
        if (!result.stdout_data.empty()) {
            std::string line = result.stdout_data.substr(0, result.stdout_data.find('\n'));
            std::filesystem::path selected(line);
            if (std::filesystem::is_regular_file(selected)) return selected;
        }

        return std::nullopt;
    };

    // Read stdin if available. The bash dispatcher only tests for a pipe
    // ([ -t 0 ]), so an empty pipe still counts as piped stdin.
    if (!isatty(STDIN_FILENO)) {
        std::string stdin_content;
        std::string line;
        while (std::getline(std::cin, line)) {
            stdin_content += line;
            stdin_content += '\n';
        }
        base_ctx.stdin_content = std::move(stdin_content);
        base_ctx.stdin_consumed = true;
    }

    // Parse chained prompts
    std::span<char const* const> argv_span(argv, static_cast<std::size_t>(argc));
    auto chain = prompt::parse_chain(argv_span, prompt::get_all_prompts());

    // If no prompts found, default to "auto"
    if (chain.count == 0) {
        chain.invocations[0].name = "auto";
        chain.count = 1;
    }

    // Pipeline for output accumulation
    prompt::pipeline pipeline;
    int exit_code = 0;

    // Execute each prompt in chain
    for (std::size_t i = 0; i < chain.count; ++i) {
        auto const& inv = chain.invocations[i];
        auto* desc = prompt::find_prompt(inv.name);
        if (!desc) {
            std::cerr << "prompt: prompt '" << inv.name << "' not found\n";
            return 3;
        }

        // Per-prompt help: prompt <name> --help / -h. The bash dispatcher
        // just runs the script with --help and keeps accumulating: the
        // help text flows through the chain like any other stdout (and the
        // chain continues with the remaining prompts).
        bool wants_help = false;
        for (std::size_t j = 0; j < inv.args_count; ++j) {
            if (inv.args[j] == "--help" || inv.args[j] == "-h") {
                wants_help = true;
                break;
            }
        }
        if (wants_help) {
            std::ostringstream help_os;
            if (!desc->sort_file.empty()) {
                // Same as the bash dispatcher: run the script with --help
                // (or print the .txt/.md file itself). Scripts shadowing
                // native prompts keep their own help text.
                prompt::legacy::render_script_help(desc->sort_file, help_os);
            } else if (desc->render_help_fn) {
                desc->render_help_fn(help_os);
            }
            pipeline.add_output(prompt::sanitize::sanitize(help_os.str()), false);
            continue;
        }

        // Build context for this prompt
        auto ctx = base_ctx;
        ctx.name = inv.name;
        // Copy invocation args into context (handle size mismatch)
        for (std::size_t j = 0; j < inv.args_count && j < ctx.args.size(); ++j) {
            ctx.args[j] = inv.args[j];
        }
        ctx.args_count = inv.args_count;

        // Execute prompt
        auto result = desc->execute_fn(std::move(ctx));

        // Sanitize output
        std::string sanitized = prompt::sanitize::sanitize(result.output);

        // Accumulate (a prompt's stdout is printed even when it failed —
        // the bash dispatcher captures output before checking status).
        pipeline.add_output(sanitized, result.no_clipboard);

        // Diagnostics go to stderr, verbatim (bash passes `>&2` output
        // through untouched) and are never sanitized or copied.
        if (!result.error.empty()) {
            std::cerr << result.error;
        }

        // First failure wins, but the chain keeps running (bash's loop
        // records the status and continues to the next prompt). Bash does
        // NOT feed one prompt's output into the next: every prompt in the
        // chain sees the original stdin — until someone reads the pipe.
        if (result.exit_code != 0 && exit_code == 0) {
            exit_code = result.exit_code;
        }
        if (result.stdin_consumed) {
            base_ctx.stdin_content.clear();
        }
    }

    // Finalize and output
    std::string final_output = pipeline.finalize();
    std::cout << final_output;

    // Copy to clipboard (respecting per-prompt no-clipboard opt-outs)
    auto clip = pipeline.for_clipboard();
    if (!clip.empty()) {
        prompt::clipboard::copy(clip);
    }

    return exit_code;
}