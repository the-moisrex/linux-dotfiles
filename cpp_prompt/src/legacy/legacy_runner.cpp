#include "prompt/legacy/legacy_runner.hpp"
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace prompt::legacy {

namespace {

namespace fs = std::filesystem;

std::string read_file(fs::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Split on '\n'; a trailing newline yields a final empty line (awk split
// semantics used by _extract-help.awk).
std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    if (text.empty()) return lines;
    std::size_t start = 0;
    while (true) {
        std::size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
        if (start == text.size()) {
            lines.push_back({});
            break;
        }
    }
    return lines;
}

// First paragraph of a help text: skip the Usage block and indented lines,
// then join the prose lines until the first blank line (port of the awk
// first_paragraph used by the bash list prompt).
std::string first_paragraph(std::string_view text) {
    std::string para;
    bool seen = false;
    for (auto line : split_lines(text)) {
        bool indented = !line.empty() && line[0] == ' ';
        if (seen && line.empty()) break;
        if (!seen && (line.starts_with("Usage:") || indented || line.empty())) continue;
        seen = true;
        if (indented) continue;
        para += line;
        para += ' ';
    }
    while (!para.empty() && para.back() == ' ') para.pop_back();
    for (char& c : para)
        if (c == '\t') c = ' ';
    return para;
}

std::string trim_trailing_newlines(std::string text) {
    while (!text.empty() && text.back() == '\n') text.pop_back();
    return text;
}

// Parse a heredoc opener (`cat <<'EOF'`, `cat <<-EOF`, ...) on a line.
// Returns the delimiter and whether it is a `<<-` (tab-stripping) form.
std::optional<std::pair<std::string, bool>> parse_heredoc_open(std::string_view line) {
    std::size_t pos = line.find("<<");
    if (pos == std::string_view::npos) return std::nullopt;
    std::size_t i = pos + 2;
    bool dash = false;
    if (i < line.size() && line[i] == '-') {
        dash = true;
        ++i;
    }
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i < line.size() && (line[i] == '\'' || line[i] == '"')) {
        char quote = line[i];
        ++i;
        std::size_t start = i;
        while (i < line.size() && line[i] != quote) ++i;
        if (i >= line.size()) return std::nullopt;
        std::string delim(line.substr(start, i - start));
        if (delim.empty()) return std::nullopt;
        return std::pair{std::move(delim), dash};
    }
    if (i >= line.size()) return std::nullopt;
    if (!(std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
        return std::nullopt;
    }
    std::size_t start = i;
    ++i;
    while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
        ++i;
    }
    return std::pair{std::string(line.substr(start, i - start)), dash};
}

fs::path find_prompt_file(std::string_view name) {
    static constexpr char const* exts[] = {".sh", ".txt", ".md", ""};
    for (auto const& dir : search_dirs()) {
        for (auto ext : exts) {
            auto candidate = dir / (std::string(name) + ext);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
    }
    return {};
}

// Restore-on-scope-exit environment mutation (single-threaded use).
struct scoped_env {
    struct entry {
        std::string key;
        std::optional<std::string> old;
    };
    std::vector<entry> entries;

    ~scoped_env() {
        for (auto const& e : entries) {
            if (e.old) {
                ::setenv(e.key.c_str(), e.old->c_str(), 1);
            } else {
                ::unsetenv(e.key.c_str());
            }
        }
    }

    void set(std::string const& key, std::string const& value) {
        std::optional<std::string> old;
        if (char const* v = ::getenv(key.c_str())) old = v;
        ::setenv(key.c_str(), value.c_str(), 1);
        entries.push_back({key, std::move(old)});
    }
};

struct marker_file {
    std::filesystem::path path;
    marker_file() {
        char tmpl[] = "/tmp/prompt_nc_XXXXXX";
        int fd = ::mkstemp(tmpl);
        if (fd >= 0) {
            ::close(fd);
            path = tmpl;
        }
    }
    ~marker_file() {
        if (!path.empty()) {
            std::error_code ec;
            fs::remove(path, ec);
        }
    }
};

process::command_result run_prompt_file(fs::path const& file, std::string_view prompt_name, std::string_view stdin_data,
                                        std::span<char const* const> args) noexcept {

    std::vector<std::string> cmd_storage;
    if (file.extension() == ".sh") {
        cmd_storage.emplace_back("bash");
        cmd_storage.emplace_back(file.string());
        for (std::size_t i = 0; i < args.size() && args[i]; ++i) {
            cmd_storage.emplace_back(args[i]);
        }
    } else {
        // Text/markdown: print the file, then append stdin. The bash
        // dispatcher passes no args to these — mirror that.
        cmd_storage.emplace_back("cat");
        cmd_storage.emplace_back("--");
        cmd_storage.emplace_back(file.string());
        if (!stdin_data.empty()) {
            cmd_storage.emplace_back("-");
        }
    }

    std::vector<char const*> argv;
    argv.reserve(cmd_storage.size() + 1);
    for (auto const& s : cmd_storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);

    // Export the dispatcher environment the bash scripts rely on.
    scoped_env env;
    env.set("PROMPT_NAME", std::string(prompt_name));
    if (::isatty(STDOUT_FILENO)) env.set("PROMPT_OUTPUT_TTY", "1");
    marker_file marker;
    if (!marker.path.empty()) {
        env.set("PROMPT_NO_CLIPBOARD_FILE", marker.path.string());
    }

    auto result = process::run_command(argv, stdin_data);

    if (!marker.path.empty()) {
        std::ifstream in(marker.path);
        std::string line;
        if (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "no-clipboard") result.no_clipboard = true;
        }
    }
    return result;
}

} // namespace

std::vector<fs::path> search_dirs() {
    std::vector<fs::path> dirs;

    // $XDG_CONFIG_DIRS/prompts (bash dispatcher searches XDG first).
    char const* xdg = std::getenv("XDG_CONFIG_DIRS");
    std::string xdg_str = xdg ? xdg : "/etc/xdg";
    std::istringstream ss(xdg_str);
    std::string part;
    while (std::getline(ss, part, ':')) {
        if (!part.empty()) dirs.push_back(fs::path(part) / "prompts");
    }

    std::error_code ec;
    auto exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return dirs;
    auto dir = exe.parent_path();

    // <exe_dir>/../prompts: the installed layout (bin/prompt -> ../prompts
    // in bash) and cpp_prompt/prompts in the build tree.
    auto one_up = dir / ".." / "prompts";
    if (fs::is_directory(one_up, ec)) dirs.push_back(one_up);

    // Walk up for the repository root, marked by bin/prompt (the bash
    // dispatcher): any build layout (build/, build-asan/tests/) then finds
    // the repo's prompts/ without climbing into unrelated ~/prompts dirs.
    for (int depth = 0; depth < 12; ++depth) {
        if (fs::exists(dir / "bin" / "prompt", ec)) {
            auto repo_prompts = dir / "prompts";
            if (fs::is_directory(repo_prompts, ec)) dirs.push_back(repo_prompts);
            break;
        }
        auto parent = dir.parent_path();
        if (parent == dir || parent.empty()) break;
        dir = parent;
    }

    return dirs;
}

process::command_result run_prompt_script(std::string_view name, std::string_view stdin_data,
                                          std::span<char const* const> args) noexcept {

    auto file = find_prompt_file(name);
    if (file.empty()) {
        return {"", "prompt: prompt '" + std::string(name) + "' not found", 3};
    }
    return run_prompt_file(file, name, stdin_data, args);
}

void render_script_help(fs::path const& file, std::ostream& os) noexcept {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        os << "prompt: prompt '" << file.filename().string() << "' not found\n";
        return;
    }
    if (file.extension() == ".sh") {
        // The bash dispatcher passes --help through to the script itself.
        std::vector<char const*> argv = {"bash", file.c_str(), "--help", nullptr};
        auto result = process::run_command(argv, "");
        if (!result.stdout_data.empty()) {
            os << result.stdout_data;
        } else if (result.exit_code != 0) {
            os << "(no --help output)\n";
        }
    } else {
        std::string content = read_file(file);
        os << content;
        if (!content.empty() && content.back() != '\n') os << '\n';
    }
}

static_help extract_static_help(fs::path const& file) noexcept {
    static_help out;

    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return out;

    if (file.extension() != ".sh") {
        std::string content = read_file(file);
        if (content.empty()) return out;
        out.found = true;
        out.full = trim_trailing_newlines(std::move(content));
        out.summary = first_paragraph(out.full);
        return out;
    }

    std::string content = read_file(file);
    if (content.empty()) return out;
    auto lines = split_lines(content);

    // Locate the show_help() function and its first heredoc — same states
    // as prompts/_extract-help.awk.
    std::size_t i = 0;
    for (; i < lines.size(); ++i) {
        auto line = lines[i];
        if (!line.starts_with("show_help")) continue;
        std::size_t j = 9; // strlen("show_help")
        while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
        if (j + 1 < line.size() && line[j] == '(' && line[j + 1] == ')') break;
    }
    if (i == lines.size()) {
        // No static heredoc: fall back to running `bash <file> --help`
        // (what the bash list prompt does for such scripts).
        std::vector<char const*> argv = {"bash", file.c_str(), "--help", nullptr};
        auto result = process::run_command(argv, "");
        if (result.stdout_data.empty()) return out;
        out.found = true;
        out.full = trim_trailing_newlines(result.stdout_data);
        out.summary = first_paragraph(result.stdout_data);
        if (out.summary.empty()) out.summary = "(no help)";
        return out;
    }

    // Find the heredoc opener before the function's closing brace.
    std::string delim;
    bool dash = false;
    bool have_delim = false;
    for (++i; i < lines.size(); ++i) {
        auto line = lines[i];
        if (auto open = parse_heredoc_open(line)) {
            delim = open->first;
            dash = open->second;
            have_delim = true;
            break;
        }
        if (!line.empty() && (line.find_first_not_of(" \t") != std::string_view::npos) &&
            line[line.find_first_not_of(" \t")] == '}') {
            break;
        }
    }
    if (!have_delim) {
        // show_help() without a heredoc: fall back like the awk does.
        std::vector<char const*> argv = {"bash", file.c_str(), "--help", nullptr};
        auto result = process::run_command(argv, "");
        if (result.stdout_data.empty()) return out;
        out.found = true;
        out.full = trim_trailing_newlines(result.stdout_data);
        out.summary = first_paragraph(result.stdout_data);
        if (out.summary.empty()) out.summary = "(no help)";
        return out;
    }

    // Collect the heredoc body (a `<<-` opener strips leading tabs).
    std::string body;
    for (++i; i < lines.size(); ++i) {
        std::string line(lines[i]);
        if (dash) {
            std::size_t first = line.find_first_not_of('\t');
            line = first == std::string::npos ? "" : line.substr(first);
        }
        if (line == delim) break;
        body += line;
        body += '\n';
    }

    out.found = true;
    out.full = trim_trailing_newlines(body);
    out.summary = first_paragraph(body);
    return out;
}

std::string help_text(std::string_view name) noexcept {
    auto file = find_prompt_file(name);
    if (file.empty()) return {};
    auto help = extract_static_help(file);
    if (!help.found || help.full.empty()) return {};
    return help.full + "\n";
}

std::filesystem::path prompts_dir() noexcept {
    for (auto const& dir : search_dirs()) {
        std::error_code ec;
        if (fs::is_regular_file(dir / "_common.sh", ec)) return dir;
    }
    return {};
}

std::filesystem::path prompt_file(std::string_view name) noexcept { return find_prompt_file(name); }

} // namespace prompt::legacy
