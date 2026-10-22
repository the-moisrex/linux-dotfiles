#include "../core/fs.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

namespace {

// The old fenced-language table, kept so CMakeLists.txt still tags as "cmake"
// (embed.cpp's infer_lang reorders CMakeLists.txt after .txt and would emit
// "text" instead).
std::string_view infer_lang(std::string_view base) {
    auto after_last_dot = base.rfind('.');
    std::string_view ext = (after_last_dot == std::string_view::npos) ? base : base.substr(after_last_dot + 1);

    if (base == "Dockerfile") return "dockerfile";
    if (base == "Makefile" || base == "makefile" || base == "GNUmakefile") return "makefile";
    if (base == "CMakeLists.txt") return "cmake";

    if (ext == "c" || ext == "h") return "c";
    if (ext == "cc" || ext == "cp" || ext == "cpp" || ext == "cxx" || ext == "c++" || ext == "hpp" || ext == "hxx" ||
        ext == "hh" || ext == "h++")
        return "cpp";
    if (ext == "m") return "objectivec";
    if (ext == "mm") return "objective-cpp";
    if (ext == "rs") return "rust";
    if (ext == "py" || ext == "pyi") return "python";
    if (ext == "sh" || ext == "bash") return "bash";
    if (ext == "zsh") return "zsh";
    if (ext == "fish") return "fish";
    if (ext == "nu") return "nu";
    if (ext == "js" || ext == "cjs" || ext == "mjs") return "javascript";
    if (ext == "ts" || ext == "mts" || ext == "cts") return "typescript";
    if (ext == "jsx") return "jsx";
    if (ext == "tsx") return "tsx";
    if (ext == "java") return "java";
    if (ext == "kt" || ext == "kts") return "kotlin";
    if (ext == "swift") return "swift";
    if (ext == "go") return "go";
    if (ext == "rb") return "ruby";
    if (ext == "php") return "php";
    if (ext == "lua") return "lua";
    if (ext == "pl" || ext == "pm") return "perl";
    if (ext == "r") return "r";
    if (ext == "scala") return "scala";
    if (ext == "cs") return "csharp";
    if (ext == "fs" || ext == "fsx") return "fsharp";
    if (ext == "vb") return "vbnet";
    if (ext == "dart") return "dart";
    if (ext == "ex" || ext == "exs") return "elixir";
    if (ext == "erl" || ext == "hrl") return "erlang";
    if (ext == "clj" || ext == "cljs" || ext == "cljc") return "clojure";
    if (ext == "ml" || ext == "mli") return "ocaml";
    if (ext == "sql") return "sql";
    if (ext == "html" || ext == "htm") return "html";
    if (ext == "css") return "css";
    if (ext == "scss") return "scss";
    if (ext == "sass") return "sass";
    if (ext == "less") return "less";
    if (ext == "xml") return "xml";
    if (ext == "xsl" || ext == "xslt") return "xslt";
    if (ext == "svg") return "svg";
    if (ext == "json" || ext == "stock" || ext == "tse") return "json";
    if (ext == "jsonc") return "jsonc";
    if (ext == "yaml" || ext == "yml") return "yaml";
    if (ext == "toml") return "toml";
    if (ext == "ini" || ext == "cfg" || ext == "conf") return "ini";
    if (ext == "env") return "dotenv";
    if (ext == "md") return "markdown";
    if (ext == "txt" || ext == "log") return "text";
    if (ext == "diff" || ext == "patch") return "diff";
    if (ext == "proto") return "proto";
    if (ext == "asm" || ext == "s" || ext == "S") return "asm";
    if (ext == "tex") return "tex";
    if (ext == "vim") return "vim";
    return "text";
}

std::string strip_trailing_newlines(std::string_view s) {
    std::string out(s);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

} // namespace

static void execute_git_files(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    auto file_args = positional_args(ctx);

    // Resolve inputs. With no args the script falls back to `select_files`
    // (fzf -m); the native dispatcher wires select_files(ctx) to that.
    std::vector<std::filesystem::path> selected;
    std::string errors;
    if (file_args.empty()) {
        selected = select_files(ctx);
    } else {
        for (auto const& arg : file_args) {
            auto resolved = resolve_input_file(ctx, arg);
            if (!resolved) {
                errors += std::format("prompt git.files: file not found: {}\n", arg);
                continue;
            }
            if (::access(resolved->c_str(), R_OK) != 0) {
                errors += "prompt git.files: file not readable: \"" + arg + "\"; resolved to \"" + resolved->string() +
                          "\"\n";
                continue;
            }
            selected.push_back(*resolved);
        }
    }

    std::optional<std::filesystem::path> git_root;
    if (!ctx.git_root.empty()) git_root = ctx.git_root;

    std::string output;
    for (auto const& file : selected) {
        if (!std::filesystem::exists(file) || !std::filesystem::is_regular_file(file)) continue;

        auto rel = fs::relative_path(file, git_root);
        auto lang = infer_lang(file.filename().string());
        std::string raw = read_file(file);

        // [ -n "$content" ] where content = $(cat) with trailing newlines stripped.
        if (strip_trailing_newlines(raw).empty()) continue;

        output += std::format("File {}", rel.string());
        if (opts.head_lines) output += " (first " + std::to_string(*opts.head_lines) + " lines)";
        output += "\n\n";
        output += std::format("```{}\n", lang);
        output += number_lines(trim_context_nl(raw, opts.head_lines), opts.numbered);
        output += "\n```\n\n";
    }

    if (!errors.empty()) {
        ctx.err = std::move(errors);
    }
    ctx.out = std::move(output);
}

static void render_help_git_files(std::ostream& os) {
    os << file_usage_help("git.files") << R"EOF(
Embeds the given files as Markdown code blocks.
File headings are printed relative to the Git repository root.
If no files are provided, `fzf -m` is used to choose from tracked files.

Options:
)EOF" << file_options_help("each embedded file");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "git.files",
    "Embeds the given files as Markdown code blocks. File headings are printed relative to the Git "
    "repository root. If no files are provided, `fzf -m` is used to choose from tracked files.",
    &execute_git_files, &render_help_git_files);

} // namespace prompt::prompts
