#include "prompt/prompts/de_export_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

// de-export.sh is_cpp_file: extension check first, then the CMakeLists.txt
// special case. `${base##*.}` keeps the whole name when there is no dot.
bool is_cpp_file(std::string const& file) noexcept {
    std::string base = std::filesystem::path(file).filename().string();
    auto pos = base.rfind('.');
    std::string ext = pos == std::string::npos ? base : base.substr(pos + 1);

    if (ext == "cpp" || ext == "hpp" || ext == "cxx" || ext == "hxx" || ext == "cc" || ext == "hh" || ext == "cppm" ||
        ext == "ixx" || ext == "mpp" || ext == "ccm")
        return true;
    if (base == "CMakeLists.txt") return true;
    return false;
}

std::string strip_trailing_newlines(std::string const& content) noexcept {
    std::size_t end = content.size();
    while (end > 0 && content[end - 1] == '\n') --end;
    return content.substr(0, end);
}

// The two greps run over the accumulated content; `^` anchors every grep line,
// so matching is done line by line.
bool matches_any_line(std::string const& content, std::regex const& re) noexcept {
    std::size_t start = 0;
    while (start <= content.size()) {
        std::size_t nl = content.find('\n', start);
        std::string line = content.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (std::regex_search(line, re)) return true;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return false;
}

} // namespace

prompt_result execute_de_export(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    // de-export.sh reads $stdin_content, which the dispatcher never exports and
    // the script never fills (it has no read_stdin), so piped stdin never
    // reaches all_content — mirror that here.
    std::vector<std::filesystem::path> cpp_files;
    std::string all_content;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file) && is_cpp_file(std::string(ctx.args[i]))) {
            cpp_files.push_back(file);
            all_content += strip_trailing_newlines(read_file(file)) + "\n";
        }
    }

    if (cpp_files.empty()) {
        return {std::string{}, 1, false, "No C++ files provided. Pass C++ files as arguments or pipe them to stdin.\n"};
    }

    static const std::regex modules_re(R"(^\s*module\s+\S|^\s*export\s+module\s+\S|^\s*import\s+\S|module\s*;)");
    static const std::regex exports_re(R"(^\s*export\s+\S|^\s*export\s*\{)");

    bool has_modules = matches_any_line(all_content, modules_re);
    bool has_exports = matches_any_line(all_content, exports_re);
    if (has_exports) has_modules = true;

    std::string output;
    output += "You are an expert C++ developer specializing in C++20 modules, build hygiene, and "
              "header design.\n";
    output += "\n";

    if (has_modules && has_exports) {
        output += "The code below uses C++20 modules and contains `export` declarations.\n";
        output += "\n";
        output += "- De-exporting:\n";
        output += "   - Analyze each `export` declaration and determine whether the exported entity "
                  "is actually consumed by external translation units.\n";
        output += "   - Internal helpers, implementation details, and symbols only used within the "
                  "module should have `export` removed.\n";
        output += "   - When de-exporting, also move the declaration to a non-exported partition "
                  "or to the implementation unit if it is not part of the public API.\n";
        output += "   - Preserve `export` on the module interface unit itself and on anything that "
                  "truly forms the public API.\n";
        output += "\n";
    } else {
        output += "\n";
    }

    output += "- Moving implementations out of headers:\n";
    output += "   - If an inline function, template specialization, or non-template function body "
              "is defined in a header (.h/.hpp) that is also a module interface (.cppm/.ixx), "
              "move the body to the corresponding implementation unit (.cpp).\n";
    output += "   - Keep only declarations in the interface when possible; prefer `export` on "
              "declarations, not definitions.\n";
    output += "   - For templates that must remain visible, keep them in the interface but mark "
              "implementation details as internal (non-exported).\n";
    output += "   - If a header is not part of a module, prefer forward declarations and moving "
              "definitions to .cpp files where feasible.\n";
    output += "\n";

    for (auto const& file : cpp_files) {
        output += "File: " + file.filename().string() + "\n\n";
        output += "```" + infer_lang(file) + "\n";
        output += trim_context_nl(read_file(file), head_lines);
        output += "\n```\n";
        output += "\n";
    }

    return {std::move(output), 0, false, std::string{}};
}

void render_help_de_export(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt de-export [--head N] [FILE...]
       some-command | prompt de-export [--head N] [FILE...]

Reduce unnecessary exports from C++20 modules and move implementations out of headers.

Only applies de-export advice if the project uses C++20 modules and contains `export`s.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
