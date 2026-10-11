#include "prompt/prompts/summarize_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

// Port of `trimmed="$(printf '%s' "$input" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"`:
// every line is stripped of leading/trailing whitespace (sed works line by
// line), then command substitution drops the result's trailing newlines.
std::string trim_lines(std::string_view input) noexcept {
    std::string out;
    std::size_t pos = 0;
    while (pos <= input.size()) {
        std::size_t nl = input.find('\n', pos);
        bool last = nl == std::string_view::npos;
        std::string_view line = last ? input.substr(pos) : input.substr(pos, nl - pos);
        std::size_t begin = 0;
        std::size_t end = line.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(line[begin])) != 0) ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(line[end - 1])) != 0) --end;
        out.append(line.substr(begin, end - begin));
        if (last) break;
        out += '\n';
        pos = nl + 1;
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::string drop_trailing_newlines(std::string_view text) noexcept {
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return std::string(text);
}

// Port of grep -Eq '^https?://[^[:space:]]+$' (summarize.sh's URL test).
bool is_url(std::string_view trimmed) noexcept {
    if (trimmed.find('\n') != std::string_view::npos) return false; // multi-line input
    std::string_view rest;
    if (trimmed.starts_with("http://")) {
        rest = trimmed.substr(7);
    } else if (trimmed.starts_with("https://")) {
        rest = trimmed.substr(8);
    } else {
        return false;
    }
    if (rest.empty()) return false; // the + needs at least one character
    for (char c : rest) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) return false;
    }
    return true;
}

int run_tool_status(std::filesystem::path const& tool, std::vector<std::string> const& args,
                    std::string_view stdin_data, std::string& stdout_data) noexcept {
    std::vector<std::string> cmd = {tool.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    std::vector<char const*> argv;
    argv.reserve(cmd.size() + 1);
    for (auto const& arg : cmd) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    auto result = prompt::process::run_command(argv, stdin_data);
    stdout_data = result.stdout_data;
    return result.exit_code;
}

// xargs: every whitespace-separated token becomes one subtitle argument (an
// empty token list still runs the command once, like GNU xargs without -r).
std::vector<std::string> xargs_tokens(std::string_view text) noexcept {
    std::vector<std::string> tokens;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t start = text.find_first_not_of(" \t\r\n", pos);
        if (start == std::string_view::npos) break;
        std::size_t end = text.find_first_of(" \t\r\n", start);
        if (end == std::string_view::npos) end = text.size();
        tokens.emplace_back(text.substr(start, end - start));
        pos = end;
    }
    return tokens;
}

// `"$curdir/yt.links" "$url" | xargs "$curdir/subtitle" | "$curdir/srt2text"`
// (under `set -o pipefail`, so any failing stage fails the whole pipeline).
std::string transform_url(std::string_view url, std::vector<std::string> const& tools, bool& ok) noexcept {
    std::size_t const links = 0;
    std::size_t const subtitle = 1;
    std::size_t const srt2text = 2;

    std::string links_out;
    int links_status = run_tool_status(tools[links], {std::string(url)}, {}, links_out);

    std::string sub_out;
    int sub_status = run_tool_status(tools[subtitle], xargs_tokens(links_out), {}, sub_out);

    std::string text_out;
    int text_status = run_tool_status(tools[srt2text], {}, sub_out, text_out);

    ok = links_status == 0 && sub_status == 0 && text_status == 0;
    return drop_trailing_newlines(text_out);
}

} // namespace

prompt_result execute_summarize(prompt_context&& ctx) noexcept {
    std::vector<std::string> tools;
    tools.reserve(3);
    for (char const* name : {"yt.links", "subtitle", "srt2text"}) {
        tools.push_back(prompt::fs::bin_tool(name, ctx.exe_path, ctx.git_root).string());
    }

    // bash checks `[ -t 0 ]`: a piped stdin wins over the clipboard.
    std::string input;
    if (ctx.stdin_consumed) {
        input = drop_trailing_newlines(ctx.stdin_content);
    } else {
        input = drop_trailing_newlines(ctx.clipboard_content());
        if (input.empty()) {
            return {std::string{}, 1, false, "No input. Pipe a URL or clipboard content.\n", false};
        }
    }

    std::string output;
    output += "Summarize this and remove the ads, repetition, meaningless stuff: \n\n";

    std::string trimmed = trim_lines(input);
    if (is_url(trimmed)) {
        bool ok = false;
        std::string result = transform_url(trimmed, tools, ok);
        if (ok) {
            output += result;
            return {std::move(output), 0, false, std::string{}, ctx.stdin_consumed};
        }
        // transform_url failed: fall back to the raw input.
    }

    output += input;
    return {std::move(output), 0, false, std::string{}, ctx.stdin_consumed};
}

void render_help_summarize(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt summarize

Builds a prompt that summarizes stdin.
If stdin is a single URL, it tries to fetch subtitles/text first.
)EOF";
}

} // namespace prompt::prompts
