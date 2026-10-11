#include "prompt/prompts/paths_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/prompts/files_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace prompt::prompts {

namespace {

std::string strip_trailing_newlines(std::string s) noexcept {
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

std::vector<std::string> split_lines(std::string const& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            if (start < text.size()) lines.push_back(text.substr(start));
            break;
        }
        if (end > start) lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// Port of paths.sh's inline hit parser: split a "path:line:col" token into the
// path (up to the first colon, minus one trailing colon) and the line number.
std::pair<std::string, std::string> parse_hit(std::string const& raw) {
    std::size_t colon = raw.find(':');
    std::string clean = colon == std::string::npos ? raw : raw.substr(0, colon);
    if (!clean.empty() && clean.back() == ':') clean.pop_back();
    std::string rest = raw.substr(clean.size());
    std::string line_num;
    if (rest.size() >= 2 && rest[0] == ':') {
        std::size_t j = 1;
        while (j < rest.size() && rest[j] >= '0' && rest[j] <= '9') ++j;
        if (j > 1) line_num = rest.substr(1, j - 1);
    }
    return {clean, line_num};
}

// git ls-files --error-unmatch <path> &>/dev/null — silence both streams.
bool git_tracked(std::string const& path) {
    std::vector<char const*> argv = {"bash", "-c", "git ls-files --error-unmatch \"$0\" >/dev/null 2>&1", path.c_str(),
                                     nullptr};
    return prompt::process::run_command(argv).exit_code == 0;
}

// `wc -l < file`: number of newline bytes.
long long count_newlines(std::string const& content) {
    long long n = 0;
    for (char c : content) {
        if (c == '\n') ++n;
    }
    return n;
}

// sed's line model — trailing newline does not create a phantom last line.
std::vector<std::string> to_sed_lines(std::string const& content) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        std::size_t end = content.find('\n', start);
        if (end == std::string::npos) {
            if (start < content.size()) lines.push_back(content.substr(start));
            break;
        }
        lines.push_back(content.substr(start, end - start));
        start = end + 1;
        if (start == content.size()) break;
    }
    return lines;
}

} // namespace

prompt_result execute_paths(prompt_context&& ctx) noexcept {
    // paths.sh parses its own args (no init_prompt): --context/--head are NOT
    // integer-validated, and --head is only consumed so it is not a path.
    std::string mode = "context";
    long long context_lines = 10;
    std::size_t head_lines = 0;
    std::vector<std::string> file_args;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        std::string a(ctx.args[i]);
        if (a == "--context") {
            if (i + 1 >= ctx.args_count) {
                return {std::string{}, 2, false, "Missing value for --context\n"};
            }
            context_lines = std::stoll(std::string(ctx.args[++i]));
        } else if (a == "--full") {
            mode = "full";
        } else if (a == "--head") {
            if (i + 1 >= ctx.args_count) {
                return {std::string{}, 2, false, "Missing value for --head\n"};
            }
            head_lines = static_cast<std::size_t>(std::stoll(std::string(ctx.args[++i])));
        } else {
            file_args.push_back(a);
        }
    }

    std::string output;
    if (ctx.stdin_consumed) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }

    std::string stdin_content = strip_trailing_newlines(ctx.stdin_content);
    std::optional<std::filesystem::path> git_root_opt =
        ctx.git_root.empty() ? std::nullopt : std::optional(ctx.git_root);

    // First-seen path -> line number, stdin hits first, then args.
    std::vector<std::string> order;
    std::vector<std::pair<std::string, std::string>> hits; // (clean, line_num)

    if (!stdin_content.empty()) {
        auto tool = fs::bin_tool("paths", ctx.exe_path, git_root_opt);
        std::vector<char const*> argv = {tool.c_str(), nullptr};
        auto result = prompt::process::run_command(argv, stdin_content);
        for (auto const& line : split_lines(result.stdout_data)) {
            auto [clean, line_num] = parse_hit(line);
            bool seen = false;
            for (auto const& h : hits) {
                if (h.first == clean) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                hits.emplace_back(clean, line_num);
                order.push_back(clean);
            }
        }
    }
    for (auto const& arg : file_args) {
        auto [clean, line_num] = parse_hit(arg);
        bool seen = false;
        for (auto const& h : hits) {
            if (h.first == clean) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            hits.emplace_back(clean, line_num);
            order.push_back(clean);
        }
    }

    if (hits.empty()) {
        return {std::move(output), 1, false, "prompt paths: no file paths found\n", ctx.stdin_consumed};
    }

    // Resolve each unique path and keep only readable, git-tracked files.
    std::vector<std::string> resolved_files;
    for (auto const& clean : order) {
        auto resolved = ctx.resolve_input_file(clean);
        if (!resolved) continue;
        if (::access(resolved->c_str(), R_OK) != 0) continue;
        if (!git_tracked(resolved->string())) continue;
        resolved_files.push_back(resolved->string());
    }

    if (resolved_files.empty()) {
        return {std::move(output), 1, false, "prompt paths: no tracked files found\n", ctx.stdin_consumed};
    }

    if (mode == "full") {
        // Delegate to the native files prompt exactly like the script
        // delegates to files.sh.
        prompt_context sub = ctx;
        sub.name = "files";
        std::size_t n = 0;
        for (auto const& r : resolved_files) sub.args[n++] = r;
        sub.args_count = n;
        auto files_result = prompts::execute_files(std::move(sub));
        output += files_result.output;
        return {std::move(output), 0, false, std::string{}, ctx.stdin_consumed};
    }

    for (auto const& resolved : resolved_files) {
        auto rel = fs::relative_path(std::filesystem::path(resolved), git_root_opt);
        auto lang = infer_lang(std::filesystem::path(resolved));

        // bash looks up the line number by the RESOLVED path (file_lines[$resolved]),
        // which only matches when resolution kept the original relative path.
        std::string line_num;
        for (auto const& h : hits) {
            if (h.first == resolved) {
                line_num = h.second;
                break;
            }
        }

        if (!line_num.empty()) {
            std::string content = read_file(std::filesystem::path(resolved));
            long long total = count_newlines(content);
            long long ln = std::stoll(line_num);
            long long start = ln - context_lines;
            if (start < 1) start = 1;
            long long end = ln + context_lines;
            if (end > total) end = total;
            auto lines = to_sed_lines(content);

            output += "File " + rel.string() + " around line " + std::to_string(ln) + " (lines " +
                      std::to_string(start) + "\u2013" + std::to_string(end) + "):\n\n";
            output += "```" + lang + "\n";
            if (start <= end && start >= 1 && static_cast<std::size_t>(end) <= lines.size()) {
                for (long long i = start; i <= end; ++i) {
                    output += lines[static_cast<std::size_t>(i - 1)];
                    output += '\n';
                }
            }
            output += "\n```\n\n";
        } else {
            // embed_file "$resolved" "$rel": leading blank, no blank after the
            // heading, then the trimmed file and its closing fence.
            output += "\nFile: " + rel.string() + "\n";
            output += "```" + lang + "\n";
            output += trim_context_nl(read_file(std::filesystem::path(resolved)), head_lines);
            output += "```\n";
        }
    }

    return {std::move(output), 0, false, std::string{}, ctx.stdin_consumed};
}

void render_help_paths(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt paths [--context N] [--full] [--head N] [FILE...]
       make 2>&1 | prompt paths [--context N] [--full] [--head N]

Extracts file paths from stdin (via bin/paths) and/or arguments, resolves each, and embeds only Git-tracked files.

Modes:
  (default)   Show N lines of context around each file:line:col hit
  --full      Embed full file contents (delegates to files.sh)

Options:
  --context N  Lines of context before/after each hit (default: 10)
  --full       Embed full files instead of context windows
  --head N     Keep only the first N lines (full mode only)
)EOF";
}

} // namespace prompt::prompts
