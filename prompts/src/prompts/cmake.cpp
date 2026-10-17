#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <regex>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

std::vector<std::string> split_lines(std::string const& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            if (start < text.size()) lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start)); // keep blank lines, like awk
        start = end + 1;
    }
    return lines;
}

// Port of the --strip-lists awk: contiguous lines that are only a source/header
// file name collapse to a single comment, reset on any other line.
std::string strip_source_lists(std::string const& content) {
    static const std::regex src_re(R"(^[ \t]*[A-Za-z0-9_./-]+\.(cpp|cc|cxx|hpp|hh|h|c|ixx)[ \t]*$)");
    std::string stripped = content;
    while (!stripped.empty() && stripped.back() == '\n') stripped.pop_back();

    std::string out;
    bool in_list = false;
    for (auto const& line : split_lines(stripped)) {
        if (std::regex_match(line, src_re)) {
            if (!in_list) {
                out += "    # ... source file list omitted ...\n";
                in_list = true;
            }
        } else {
            out += line;
            out += '\n';
            in_list = false;
        }
    }
    return out;
}

} // namespace

static void execute_cmake(prompt_context& ctx) {
    // init_prompt -> parse_arguments runs first (validates --head); the script
    // then walks the remaining args for its own flags.
    auto head_lines = head_lines_or_throw(ctx);
    auto remaining = positional_args(ctx);

    std::vector<std::string> exclude_patterns;
    bool strip_lists = false;
    for (std::size_t i = 0; i < remaining.size(); ++i) {
        std::string_view a = remaining[i];
        if (a == "--exclude") {
            if (i + 1 >= remaining.size()) {
                throw usage_error{"Missing value for --exclude\n"};
            }
            exclude_patterns.emplace_back(remaining[++i]);
        } else if (a == "--strip-lists") {
            strip_lists = true;
        }
        // any other token is discarded, exactly like the script's `*)` branch.
    }

    std::string output;
    output += "Here is the complete CMake configuration for the current project.\nPlease review the build setup, "
              "dependencies, and target structures.\n\n";

    std::vector<std::string> files;
    std::string search_dir;
    if (!ctx.git_root.empty()) {
        std::vector<std::string> storage = {"bash", "-c", R"(git -C "$0" ls-files -- "$@" 2>/dev/null)"};
        storage.push_back(ctx.git_root.string());
        storage.emplace_back("*CMakeLists.txt");
        storage.emplace_back("*.cmake");
        files = split_lines(prompt::process::run_command(storage).stdout_data);
        search_dir = ctx.git_root.string();
    } else {
        for (auto& f :
             split_lines(prompt::process::run_command(
                             {"bash", "-c", R"(find . -type f \( -name CMakeLists.txt -o -name "*.cmake" \))"})
                             .stdout_data)) {
            if (f.starts_with("./")) f = f.substr(2);
            files.push_back(f);
        }
        search_dir = ".";
    }

    for (auto const& f : files) {
        bool exclude = false;
        for (auto const& pat : exclude_patterns) {
            if (f.contains(pat)) {
                exclude = true;
                break;
            }
        }
        if (exclude) continue;

        std::filesystem::path file_path = std::filesystem::path(search_dir) / f;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file_path, ec)) continue;

        std::string content = read_file(file_path);
        if (strip_lists) content = strip_source_lists(content);

        output += std::format("File: {}\n", f);
        output += "\n    ```cmake\n";
        output += trim_context_nl(content, head_lines);
        output += "\n    ```\n\n";
    }

    ctx.out = std::move(output);
}

static void render_help_cmake(std::ostream& os) {
    os << R"EOF(Usage: prompt cmake [--head N] [--exclude PATTERN] [--strip-lists]

Gathers all CMakeLists.txt and *.cmake files in the current project to provide full context on the CMake build setup.

Options:
  --head N          Keep only the first N lines of each file
  --exclude PATTERN Exclude files or directories matching the pattern (can be specified multiple times)
  --strip-lists     Replace long lists of source files (.c, .cpp, .h, etc.) with a comment to save context window space
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("cmake",
                            "Gathers all CMakeLists.txt and *.cmake files in the current project to provide full "
                            "context on the CMake build setup.",
                            &execute_cmake, &render_help_cmake);

} // namespace prompt::prompts
