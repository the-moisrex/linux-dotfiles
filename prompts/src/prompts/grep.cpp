#include "../core/process.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/schema.hpp"
#include "../sdk/stage.hpp"
#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

struct grep_options {
    std::string pattern;
    std::string path = ".";
    bool ignore_case = false;
    std::vector<std::string> include;
    std::size_t max = 0;
    std::size_t head = 0;
};

grep_options parse_grep(view const& v) {
    grep_options opts;
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "-i" || arg == "--ignore-case") {
            opts.ignore_case = true;
        } else if (arg == "--include") {
            if (++i >= v.args.size()) throw usage_error{"grep: --include needs a glob"};
            opts.include.emplace_back(v.args[i]);
        } else if (arg == "--max") {
            if (++i >= v.args.size()) throw usage_error{"grep: --max needs a number"};
            try {
                opts.max = static_cast<std::size_t>(std::stoull(std::string(v.args[i])));
            } catch (...) {
                throw usage_error{"grep: --max needs a number"};
            }
        } else if (arg == "--head") {
            if (++i >= v.args.size()) throw usage_error{"grep: --head needs a number"};
            try {
                opts.head = static_cast<std::size_t>(std::stoull(std::string(v.args[i])));
            } catch (...) {
                throw usage_error{"grep: --head needs a number"};
            }
        } else if (arg.size() > 1 && arg.front() == '-') {
            throw usage_error{"grep: unknown option " + std::string(arg)};
        } else if (opts.pattern.empty()) {
            opts.pattern = std::string(arg);
        } else if (opts.path == ".") {
            opts.path = std::string(arg);
        } else {
            throw usage_error{"grep: too many arguments (PATTERN [PATH])"};
        }
    }
    if (opts.pattern.empty()) throw usage_error{"grep: a PATTERN is required"};
    return opts;
}

std::string run_grep(grep_options const& opts) {
    if (process::run_script("command -v rg >/dev/null 2>&1").exit_code == 0) {
        std::vector<std::string> argv{
            "rg", "--line-number", "--no-heading", "--color", "never", "--with-filename", "--sort", "path",
        };
        if (opts.ignore_case) argv.emplace_back("-i");
        for (auto const& glob : opts.include) {
            argv.emplace_back("-g");
            argv.emplace_back(glob);
        }
        if (opts.max > 0) {
            argv.emplace_back("-m");
            argv.emplace_back(std::to_string(opts.max));
        }
        argv.emplace_back("-e");
        argv.emplace_back(opts.pattern);
        argv.emplace_back(opts.path);
        auto const res = process::command(argv).discard_stderr().run();
        if (res.exit_code == 0 || res.exit_code == 1) return res.stdout_data;
    }
    // Fallback: recursive grep. `-I` skips binaries, `-n` numbers lines. The
    // include globs are a ripgrep nicety; the plain recursive form is the
    // useful fallback.
    std::string script = opts.ignore_case ? "grep -rnIi " : "grep -rnI ";
    script += R"(-e "$1" "$2" 2>/dev/null)";
    return process::run_script(script, {"grep", opts.pattern, opts.path}).stdout_data;
}

void complete_grep(completion_request const& req, std::ostream& os) {
    if (auto const* tool = schema::find_tool("grep")) schema::complete(*tool, req, os);
}

constexpr std::string_view kGrepSummary =
    "Search file contents for a pattern with ripgrep (like opencode's grep tool), printing path:line:text.";

schema::tool grep_schema() {
    return schema::make_tool(
        "grep", std::string(kGrepSummary),
        {
            schema::bool_flag("ignore-case", "Case-insensitive search", "i"),
            schema::str_flag("include", "Only search files matching this glob (repeatable)", "GLOB"),
            schema::int_flag("max", "Stop after N matches per file"),
            schema::int_flag("head", "Keep only the first N matching lines"),
        },
        {
            schema::argument("PATTERN", "Regular expression to search for", true),
            schema::argument("PATH", "File or directory to search (default: the current directory)"),
        });
}

} // namespace

static void execute_grep(view const& v, result& r) {
    grep_options const opts = parse_grep(v);
    std::string out = run_grep(opts);

    if (opts.head > 0) {
        std::size_t lines = 0;
        std::size_t cut = out.size();
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (out[i] != '\n') continue;
            if (++lines >= opts.head) {
                cut = i + 1;
                break;
            }
        }
        out.resize(cut);
    }
    if (out.empty()) throw soft_error{"grep: no matches\n"};
    r.out = std::move(out);
}

static void render_help_grep(std::ostream& os) {
    os << R"EOF(Usage: prompt grep [options] PATTERN [PATH]

Search the contents of files under PATH (default: the current directory) for the
regular expression PATTERN, printing `path:line:text` per match via ripgrep
(`rg`) and falling back to `grep -rnI`. Use `--include` to restrict the files
searched; binary files are skipped.

Examples:
  prompt grep 'TODO|FIXME' --include '*.cpp' prompts/src
  prompt grep -i 'http' README.md
  prompt grep --head 30 'class ' | prompt .extract code

)EOF";
    os << schema::render_options(*schema::find_tool("grep"));
}

// The schema builds a few strings at startup; the registry itself never drops a
// tool (it aborts on overflow), so the allocation is fine here.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
bool const registered_grep_schema = schema::register_tool(grep_schema());

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp). The summary is a
// static-storage string_view; `register_prompt` keeps the view, so never pass a
// temporary std::string here.
bool const registered_grep =
    prompt::register_prompt("grep", kGrepSummary, &execute_grep, &render_help_grep, &complete_grep);

} // namespace prompt::prompts
