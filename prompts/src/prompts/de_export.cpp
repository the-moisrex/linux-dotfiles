#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

// de-export.sh is_cpp_file: extension check first, then the CMakeLists.txt
// special case. `${base##*.}` keeps the whole name when there is no dot.
bool is_cpp_file(std::string const& file) {
    std::string base = std::filesystem::path(file).filename().string();
    auto pos = base.rfind('.');
    std::string ext = pos == std::string::npos ? base : base.substr(pos + 1);

    if (ext == "cpp" || ext == "hpp" || ext == "cxx" || ext == "hxx" || ext == "cc" || ext == "hh" || ext == "cppm" ||
        ext == "ixx" || ext == "mpp" || ext == "ccm")
        return true;
    if (base == "CMakeLists.txt") return true;
    return false;
}

std::string strip_trailing_newlines(std::string const& content) {
    std::size_t end = content.size();
    while (end > 0 && content[end - 1] == '\n') --end;
    return content.substr(0, end);
}

// The two greps run over the accumulated content; `^` anchors every grep line,
// so matching is done line by line.
bool matches_any_line(std::string const& content, std::regex const& re) {
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

static void execute_de_export(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    // de-export.sh reads $stdin_content, which the dispatcher never exports and
    // the script never fills (it has no read_stdin), so piped stdin never
    // reaches all_content — mirror that here.
    std::vector<std::filesystem::path> cpp_files;
    std::string all_content;
    for (auto const& arg : positional_args(ctx)) {
        std::filesystem::path file(arg);
        if (std::filesystem::is_regular_file(file) && is_cpp_file(std::string(arg))) {
            cpp_files.push_back(file);
            all_content += strip_trailing_newlines(read_file(file)) + "\n";
        }
    }

    if (cpp_files.empty()) {
        throw soft_error{"No C++ files provided. Pass C++ files as arguments or pipe them to stdin.\n"};
    }

    static const std::regex modules_re(R"(^\s*module\s+\S|^\s*export\s+module\s+\S|^\s*import\s+\S|module\s*;)");
    static const std::regex exports_re(R"(^\s*export\s+\S|^\s*export\s*\{)");

    bool has_modules = matches_any_line(all_content, modules_re);
    bool has_exports = matches_any_line(all_content, exports_re);
    if (has_exports) has_modules = true;

    std::string output;
    output += R"PROMPT(You are an expert C++ developer specializing in C++20 modules, build hygiene, and header design.
)PROMPT";
    output += '\n';

    if (has_modules && has_exports) {
        output += R"PROMPT(The code below uses C++20 modules and contains `export` declarations.

- De-exporting:
   - Analyze each `export` declaration and determine whether the exported entity is actually consumed by external translation units.
   - Internal helpers, implementation details, and symbols only used within the module should have `export` removed.
   - When de-exporting, also move the declaration to a non-exported partition or to the implementation unit if it is not part of the public API.
   - Preserve `export` on the module interface unit itself and on anything that truly forms the public API.
)PROMPT";
        output += '\n';
    } else {
        output += '\n';
    }

    output += R"PROMPT(- Moving implementations out of headers:
   - If an inline function, template specialization, or non-template function body is defined in a header (.h/.hpp) that is also a module interface (.cppm/.ixx), move the body to the corresponding implementation unit (.cpp).
   - Keep only declarations in the interface when possible; prefer `export` on declarations, not definitions.
   - For templates that must remain visible, keep them in the interface but mark implementation details as internal (non-exported).
   - If a header is not part of a module, prefer forward declarations and moving definitions to .cpp files where feasible.
)PROMPT";
    output += '\n';

    for (auto const& file : cpp_files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n\n";
    }

    ctx.out = std::move(output);
}

static void render_help_de_export(std::ostream& os) {
    os << file_usage_help("de-export") << R"EOF(
Reduce unnecessary exports from C++20 modules and move implementations out of headers.

Only applies de-export advice if the project uses C++20 modules and contains `export`s.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("de-export",
                            "Reduce unnecessary exports from C++20 modules and move implementations out "
                            "of headers.",
                            &execute_de_export, &render_help_de_export);

} // namespace prompt::prompts
