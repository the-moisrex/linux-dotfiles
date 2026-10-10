#include "prompt/prompts/run_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/core/sanitize.hpp"
#include "prompt/legacy/legacy_runner.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace prompt::prompts {

namespace {

// Port of run.sh extract_failed_gtests: `[  FAILED  ] Suite.Test` lines, each
// test kept once, and only names that carry a suite separator.
std::vector<std::string> extract_failed_gtests(std::string_view text) noexcept {
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
            if (test.find('.') != std::string::npos && seen.insert(test).second) {
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
std::string head_n(std::string_view text, std::size_t n) noexcept {
    if (text.empty()) return {};
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
        if (start == text.size()) break;
    }
    if (lines.size() <= n) return std::string(text);
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

} // namespace

prompt_result execute_run(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

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
        auto run_path = (legacy::prompts_dir().parent_path() / "bin" / "run").string();
        std::vector<std::string> cmd;
        cmd.push_back(run_path);
        for (auto const& arg : run_args) cmd.push_back(arg);

        // run.sh redirects both streams into one file: >tmp 2>&1
        std::vector<std::string> storage;
        storage.push_back("bash");
        storage.push_back("-c");
        storage.push_back("\"$0\" \"$@\" 2>&1");
        storage.push_back(run_path);
        for (std::size_t i = 1; i < cmd.size(); ++i) storage.push_back(cmd[i]);

        std::vector<char const*> argv;
        argv.reserve(storage.size() + 1);
        for (auto const& s : storage) argv.push_back(s.c_str());
        argv.push_back(nullptr);

        auto result = prompt::process::run_command(argv, ctx.stdin_content);
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
    if (head_lines > 0) {
        run_output = head_n(run_output, head_lines);
    }

    std::string output;
    output += "Debug the output from " + description +
              ". Identify the root cause, explain the failure clearly, and suggest the smallest "
              "useful fix. If the build succeeded but the program failed at runtime, focus on the "
              "runtime issue.\n\n";
    output += "Exit status: " + status_text + "\n\n";
    output += "```text\n" + run_output + "\n```\n";

    if (include_gtest) {
        auto failed = extract_failed_gtests(search_output);
        if (!failed.empty()) {
            output += "\n";
            std::vector<std::string> gtest_args;
            gtest_args.push_back("--exact");
            for (auto const& name : failed) gtest_args.push_back(name);

            std::vector<char const*> argv;
            argv.reserve(gtest_args.size() + 1);
            for (auto const& s : gtest_args) argv.push_back(s.c_str());
            argv.push_back(nullptr);

            auto result = legacy::run_prompt_script("gtest-case", ctx.stdin_content, argv);
            output += result.stdout_data;
            if (!result.stderr_data.empty()) {
                // stderr of a script is never captured by popen; nothing to add
            }
        } else {
            output += "\nNo failed Google Test cases were detected in the output.\n";
        }
    }

    return {std::move(output), 0, false, std::string{}};
}

void render_help_run(std::ostream& os) noexcept {
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

} // namespace prompt::prompts
