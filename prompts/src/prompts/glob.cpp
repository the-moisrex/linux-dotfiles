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

struct glob_options {
    std::string pattern;
    std::string path;
    std::size_t head = 0;
};

glob_options parse_glob(view const& v) {
    glob_options opts;
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--path") {
            if (++i >= v.args.size()) throw usage_error{"glob: --path needs a directory"};
            opts.path = std::string(v.args[i]);
        } else if (arg == "--head") {
            if (++i >= v.args.size()) throw usage_error{"glob: --head needs a number"};
            try {
                opts.head = static_cast<std::size_t>(std::stoull(std::string(v.args[i])));
            } catch (...) {
                throw usage_error{"glob: --head needs a number"};
            }
        } else if (arg.size() > 1 && arg.front() == '-') {
            throw usage_error{"glob: unknown option " + std::string(arg)};
        } else if (opts.pattern.empty()) {
            opts.pattern = std::string(arg);
        } else {
            throw usage_error{"glob: at most one PATTERN"};
        }
    }
    if (opts.path.empty()) opts.path = ".";
    return opts;
}

bool have_rg() {
    auto const res = process::run_script("command -v rg >/dev/null 2>&1");
    return res.exit_code == 0;
}

std::string run_glob(glob_options const& opts) {
    if (have_rg()) {
        std::vector<std::string> argv{"rg", "--files", "--color", "never", "--sort", "path"};
        if (!opts.pattern.empty() && opts.pattern != "*") {
            argv.emplace_back("-g");
            argv.emplace_back(opts.pattern);
        }
        argv.emplace_back(opts.path);
        auto const res = process::command(argv).discard_stderr().run();
        if (res.exit_code == 0 || res.exit_code == 1) return res.stdout_data;
    }
    // Fallback: find only understands a basename glob, which is the common case.
    std::string const name = opts.pattern.empty() ? "*" : opts.pattern;
    auto const res = process::run_script(R"(find "$1" -type f -name "$2" 2>/dev/null | sort | sed 's|^\./||')",
                                         {"glob", opts.path, name});
    return res.stdout_data;
}

void complete_glob(completion_request const& req, std::ostream& os) {
    if (auto const* tool = schema::find_tool("glob")) schema::complete(*tool, req, os);
}

constexpr std::string_view kGlobSummary =
    "List the files matching a glob pattern (like opencode's glob tool), sorted by path.";

schema::tool glob_schema() {
    return schema::make_tool(
        "glob", std::string(kGlobSummary),
        {
            schema::str_flag("path", "Directory to search in (default: the git root, then the current directory)",
                             "DIR"),
            schema::int_flag("head", "Keep only the first N paths"),
        },
        {
            schema::argument("PATTERN", "Glob pattern to match, e.g. '**/*.cpp' (default: every file)"),
        });
}

} // namespace

static void execute_glob(view const& v, result& r) {
    glob_options const opts = parse_glob(v);
    std::string out = run_glob(opts);

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
    r.out = std::move(out);
}

static void render_help_glob(std::ostream& os) {
    os << R"EOF(Usage: prompt glob [--path DIR] [--head N] [PATTERN]

List the files matching PATTERN, sorted by path, using ripgrep (`rg --files -g`)
and falling back to `find`. With no PATTERN every file is listed. Paths are
printed as `rg` returns them (relative to DIR).

Examples:
  prompt glob '**/*.cpp'
  prompt glob --path src '*.hpp' | prompt .files
  prompt glob --head 50

)EOF";
    os << schema::render_options(*schema::find_tool("glob"));
}

// The schema builds a few strings at startup; the registry itself never drops a
// tool (it aborts on overflow), so the allocation is fine here.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
bool const registered_glob_schema = schema::register_tool(glob_schema());

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp). The summary is a
// static-storage string_view; `register_prompt` keeps the view, so never pass a
// temporary std::string here.
bool const registered_glob =
    prompt::register_prompt("glob", kGlobSummary, &execute_glob, &render_help_glob, &complete_glob);

} // namespace prompt::prompts
