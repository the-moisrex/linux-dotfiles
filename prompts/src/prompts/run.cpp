#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../core/sanitize.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace prompt::prompts {

namespace {

// Port of run.sh extract_failed_gtests: `[  FAILED  ] Suite.Test` lines, each
// test kept once, and only names that carry a suite separator.
std::vector<std::string> extract_failed_gtests(std::string_view text) {
    static const std::regex failed_re(R"(^\[  FAILED  \] +([[:alnum:]_][^\s(),]*))");
    std::vector<std::string> found;
    std::unordered_set<std::string> seen;

    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        std::string_view line = text.substr(start, end == std::string_view::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        std::smatch match;
        std::string line_str(line);
        if (std::regex_search(line_str, match, failed_re)) {
            std::string test = match[1];
            if (test.contains('.') && seen.insert(test).second) {
                found.push_back(std::move(test));
            }
        }

        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return found;
}

// Port of `head -n N` feeding a $(...) capture: first N lines, no trailing
// newline (a missing trailing newline in the input is preserved as-is).
std::string head_n(std::string_view text, std::size_t n) { return head_output(text, n); }

} // namespace

static void execute_run(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    bool include_gtest = false;
    std::vector<std::string> run_args;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
        } else if (ctx.args[i] == "--gtest") {
            include_gtest = true;
        } else {
            run_args.emplace_back(ctx.args[i]);
        }
    }

    std::string run_output;
    std::string status_text;
    std::string description;

    if (!run_args.empty()) {
        auto run_path = prompt::fs::bin_tool("run", ctx.exe_path, ctx.git_root).string();
        std::vector<std::string> cmd;
        cmd.push_back(run_path);
        for (auto const& arg : run_args) cmd.push_back(arg);

        // run.sh redirects both streams into one file: >tmp 2>&1
        std::vector<std::string> storage = {"bash", "-c", R"("$0" "$@" 2>&1)", run_path};
        for (std::size_t i = 1; i < cmd.size(); ++i) storage.push_back(cmd[i]);

        auto result = prompt::process::run_command(storage, ctx.stdin_content);
        run_output = prompt::sanitize::strip_osc(result.stdout_data);
        while (!run_output.empty() && run_output.back() == '\n') run_output.pop_back();
        status_text = std::to_string(result.exit_code);
        description = "the `run` command";
    } else {
        // run.sh only looks at $stdin_content, which _common.sh initializes
        // empty and this script never fills in, so piped input is ignored.
        status_text = "unknown";
        description = "the piped `run` output";
    }

    std::string search_output = run_output;
    if (head_lines) {
        run_output = head_n(run_output, *head_lines);
    }

    std::string output;
    std::string error;
    output += "Debug the output from " + description +
              ". Identify the root cause, explain the failure clearly, and suggest the smallest "
              "useful fix. If the build succeeded but the program failed at runtime, focus on the "
              "runtime issue.\n\n";
    output += std::format("Exit status: {}\n\n", status_text);
    output += std::format("```text\n{}\n```\n", run_output);

    if (include_gtest) {
        auto failed = extract_failed_gtests(search_output);
        if (!failed.empty()) {
            output += '\n';
            std::vector<std::string> gtest_args;
            gtest_args.emplace_back("--exact");
            for (auto const& name : failed) gtest_args.push_back(name);

            // Native gtest-case prompt, invoked in-process exactly as the
            // script would be (`gtest-case` with --exact + the test names).
            prompt_context sub = ctx;
            sub.name = "gtest-case";
            std::size_t n = 0;
            for (auto const& a : gtest_args) sub.args[n++] = a;
            sub.args_count = n;
            clear_output(sub);
            if (const auto* gtest_case = prompt::find_prompt("gtest-case")) {
                gtest_case->execute_fn(sub);
                output += sub.out;
                error += sub.err;
            }
        } else {
            output += "\nNo failed Google Test cases were detected in the output.\n";
        }
    }

    ctx.out = std::move(output);
}

static void render_help_run(std::ostream& os) {
    os << R"EOF(Usage: prompt run [--head N] [--gtest] [run-args...]
       run target | prompt run [--head N] [--gtest]

Runs `bin/run` with the provided arguments and builds a debugging prompt from its output.
If stdin is piped in, it debugs the piped run output instead.

Options:
  --head N   Keep only the first N lines of run output
  --gtest    Include source for failed Google Test cases
  -h, --help Show this help
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "run",
    "Runs `bin/run` with the provided arguments and builds a debugging prompt from its output. If "
    "stdin is piped in, it debugs the piped run output instead.",
    &execute_run, &render_help_run);

} // namespace prompt::prompts
