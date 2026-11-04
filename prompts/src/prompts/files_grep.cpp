#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace prompt::prompts {
namespace {

constexpr std::string_view kFilesGrepSummary =
    "Embed files whose contents match a regular expression as Markdown code blocks.";
constexpr std::string_view kReadGrepSummary =
    "Read files whose contents match a regular expression with numbered lines.";

enum class search_backend { automatic, rg, grep };

void execute_matching_files(view const& v, result& r, std::string_view target) {
    auto fail = [&](std::string const& message) {
        throw usage_error{std::string(v.name) + ": " + message + "\n"};
    };
    auto backend = search_backend::automatic;
    bool ignore_case = false;
    std::vector<std::string> includes;
    std::vector<std::string> forwarded;
    std::vector<std::string> operands;
    bool literal = false;
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        auto const arg = v.args[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= v.args.size()) fail(std::string(arg) + " requires a value");
            return std::string(v.args[++i]);
        };
        if (literal) {
            operands.emplace_back(arg);
        } else if (arg == "--") {
            literal = true;
        } else if (arg == "--rg" || arg == "--grep") {
            auto const selected = arg == "--rg" ? search_backend::rg : search_backend::grep;
            if (backend != search_backend::automatic && backend != selected) {
                fail("--rg and --grep are mutually exclusive");
            }
            backend = selected;
        } else if (arg == "-i" || arg == "--ignore-case") {
            ignore_case = true;
        } else if (arg == "--include") {
            includes.push_back(value());
        } else if (arg == "--head" || (arg == "--offset" && target == "read")) {
            // Only route options here; the target owns their validation and semantics.
            forwarded.emplace_back(arg);
            forwarded.push_back(value());
        } else if (is_number_flag(arg)) {
            forwarded.emplace_back(arg);
        } else if (arg.size() > 1 && arg.front() == '-') {
            fail("unknown option " + std::string(arg) + "; use -- before a dash-leading pattern");
        } else {
            operands.emplace_back(arg);
        }
    }
    if (operands.empty() || operands.size() > 2) fail("expected PATTERN [PATH]");

    // Probe availability, not search success: invalid regexes and I/O errors
    // must not silently switch engines. Explicit choices never fall back.
    if (backend == search_backend::automatic) {
        backend = process::run_script("command -v rg >/dev/null 2>&1").exit_code == 0
                      ? search_backend::rg : search_backend::grep;
    }
    bool const use_rg = backend == search_backend::rg;
    std::vector<std::string> search = use_rg
        ? std::vector<std::string>{"rg", "--no-config", "--files-with-matches", "--null", "--color", "never"}
        : std::vector<std::string>{"grep", "-r", "-I", "-l", "-Z", "-E", "--color=never", "--exclude-dir=.git"};
    if (ignore_case) search.emplace_back("-i");
    for (auto const& glob : includes) {
        if (use_rg) {
            search.emplace_back("--glob");
            search.push_back(glob);
        } else {
            search.push_back("--include=" + glob);
        }
    }
    auto const engine = search.front();
    search.emplace_back("-e");
    search.push_back(operands.front());
    search.emplace_back("--");
    search.push_back(operands.size() == 2 ? operands[1] : ".");
    auto const found = process::command(std::move(search)).capture_stderr().run();
    if (found.timed_out || (found.exit_code != 0 && found.exit_code != 1)) {
        throw soft_error{std::string(v.name) + ": " + engine + " failed (exit " +
                         std::to_string(found.exit_code) + ")\n" + found.stderr_data + "\n"};
    }
    if (found.exit_code == 1 || found.stdout_data.empty()) {
        throw soft_error{std::string(v.name) + ": no matches\n"};
    }

    // NUL-delimited records preserve spaces, colons and newlines in names.
    std::vector<std::string> paths;
    for (std::size_t begin = 0; begin < found.stdout_data.size();) {
        auto const end = found.stdout_data.find('\0', begin);
        if (end == std::string::npos || end == begin) {
            throw soft_error{std::string(v.name) + ": malformed " + engine + " path output\n"};
        }
        paths.push_back(found.stdout_data.substr(begin, end - begin));
        begin = end + 1;
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    for (auto const& path : paths) {
        // Absolute paths cannot be mistaken for the target prompt's options.
        std::error_code ec;
        auto absolute = std::filesystem::absolute(path, ec);
        if (ec) throw soft_error{std::string(v.name) + ": " + ec.message() + "\n"};
        forwarded.push_back(absolute.string());
    }
    std::vector<std::string_view> args(forwarded.begin(), forwarded.end());
    // No stdin: read would otherwise prefer the pipe over the matched files.
    // The parent's shared stdin remains available to later chain segments.
    r = run_sub_prompt(v, target, args);
    r.err += found.stderr_data;
}

void render_matching_help(std::ostream& os, std::string_view name,
                          std::string_view summary, bool read) {
    os << summary << "\n\n"
       << file_usage_help(name, "[search-options] [--] PATTERN [PATH]")
       << "\nSearch file contents under PATH (default: the current directory).\n"
          "Prefer rg on PATH; otherwise use GNU grep. Explicit choices never fall back.\n"
          "Embed each matching file once, sorted by path, rather than matching lines.\n"
          "rg respects its default ignore and hidden-file rules. grep searches hidden\n"
          "and ignored files too, excluding .git directories. Both skip binary files.\n"
          "Regex and include-glob syntax follow the selected engine (grep uses ERE\n"
          "and basename globs; rg uses Rust regexes and gitignore-style globs).\n"
          "No matches is an error; no file picker is opened.\n"
          "File options and rendering are delegated to files/read unchanged.\n"
          "Piped stdin is left available for later chain segments.\n\n"
          "Options:\n"
          "  --rg               Require ripgrep\n"
          "  --grep             Require GNU grep\n"
          "  -i, --ignore-case  Search case-insensitively\n"
          "  --include GLOB     Restrict searched files (repeatable)\n"
          "  --                 End options (allows dash-leading patterns)\n";
    if (read) os << "  --offset N         Start reading each file at line N (1-based)\n";
    os << file_options_help("each matching file", true);
    if (read) os << "\nLike read, output is always numbered and defaults to 2000 lines per file.\n";
}

} // namespace

static void execute_files_grep(view const& v, result& r) {
    execute_matching_files(v, r, "files");
}

static void execute_read_grep(view const& v, result& r) {
    execute_matching_files(v, r, "read");
}

static void render_help_files_grep(std::ostream& os) {
    render_matching_help(os, "files.grep", kFilesGrepSummary, false);
}

static void render_help_read_grep(std::ostream& os) {
    render_matching_help(os, "read.grep", kReadGrepSummary, true);
}

bool const registered_files_grep = register_prompt(
    "files.grep", kFilesGrepSummary, &execute_files_grep, &render_help_files_grep);
bool const registered_read_grep = register_prompt(
    "read.grep", kReadGrepSummary, &execute_read_grep, &render_help_read_grep);

} // namespace prompt::prompts
