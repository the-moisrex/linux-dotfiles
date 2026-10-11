#include "prompt/prompts/fs8_mod_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>
#include <string_view>
#include <system_error>

namespace prompt::prompts {

namespace {

// Port of _common.sh infer_lang (NOT the SDK's infer_lang, whose extension
// table differs — e.g. .ixx maps to "text" in bash but "cpp" in the SDK).
std::string bash_infer_lang(std::string const& filename) noexcept {
    std::string base = filename;
    auto slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    std::string ext;
    auto dot = base.find_last_of('.');
    if (dot != std::string::npos) ext = base.substr(dot + 1);
    else
        ext = base;

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

// Port of _common.sh embed_file: warns to stderr and emits nothing for a
// missing file; otherwise a blank line, "File: <label>", ```lang fence, the
// head-capped body, and a closing fence. infer_lang keys off the basename.
void embed_file(std::string& out, std::string& err, std::filesystem::path const& path, std::string_view label,
                std::size_t head_lines) noexcept {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        err += "Warning: context file not found: " + path.string() + "\n";
        return;
    }
    std::string base = path.filename().string();
    std::string name = label.empty() ? base : std::string(label);
    out += "\n";
    out += "File: " + name + "\n";
    out += "```" + bash_infer_lang(base) + "\n";
    out += trim_context_nl(read_file(path), head_lines);
    out += "```\n";
}

// Port of find_foresight_root: walk from $PWD to / looking for AGENTS.md + mods/.
std::filesystem::path find_foresight_root() noexcept {
    auto dir = std::filesystem::current_path();
    while (dir != dir.root_path() && dir != std::filesystem::path("/")) {
        if (std::filesystem::is_regular_file(dir / "AGENTS.md") && std::filesystem::is_directory(dir / "mods")) {
            return dir;
        }
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

} // namespace

prompt_result execute_fs8_mod(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    auto root = find_foresight_root();
    if (root.empty()) {
        return {std::string{}, 1, false,
                "Error: not inside the foresight git repo (could not find AGENTS.md + mods/).\n"
                "Change to the foresight directory or a subdirectory and try again.\n"};
    }

    std::string out;
    std::string err;

    out += "You are an expert C++26 developer writing a new pipeline mod for the foresight project.\n";
    out += "\n";
    out += "Foresight is a Linux input manager: a C++26 library (libforesight) plus the foresight CLI.\n";
    out += "Mods are consteval-copyable callables in the fs8 namespace that chain via operator| in a pipeline.\n";
    out += "\n";
    out += "Your task: generate a complete, correct, buildable mod that follows all project conventions.\n";
    out += "\n";

    out += "## Project overview\n";
    embed_file(out, err, root / "AGENTS.md", "foresight/AGENTS.md", head_lines);

    out += "\n";
    out += "## How to write a mod (guide)\n";
    out += "\n";
    out += "### Checklist for a new mod\n"
           "\n"
           "1. Create `mods/<name>.ixx` as `export module fs8.mods:<name>;`\n"
           "   - Define `basic_<name>` in `export namespace fs8`, inheriting `consteval_copyable`\n"
           "     (or `pimpl_idiom<basic_<name>>` if it holds runtime state).\n"
           "   - Expose a constexpr instance `<name>`.\n"
           "2. Create `mods/<name>.cxx` as `module fs8.mods;` with the `impl` definition and method bodies.\n"
           "   - Skip this file if the mod is header-only (no state, all methods inline).\n"
           "3. Register both files in the root `CMakeLists.txt`:\n"
           "   - `.cxx` \xE2\x86\x92 `target_sources(... PRIVATE ...)`\n"
           "   - `.ixx` \xE2\x86\x92 `PUBLIC FILE_SET foresight TYPE CXX_MODULES FILES`\n"
           "4. Add `export import :<name>;` to `mods/mods.ixx`.\n"
           "5. `static_assert` the `Modifier`/`OutputModifier` concept where relevant and\n"
           "   any inter-mod dependency.\n"
           "6. Handle lifecycle tags via `operator()(special_event const&)`:\n"
           "   - Return `next`/`drop_event` for ordinary events.\n"
           "   - All invocations must be `noexcept`.\n"
           "\n"
           "### Mod invocation forms\n"
           "\n"
           "- `mod(ctx)` \xE2\x80\x94 sees the whole context (event + sibling mods).\n"
           "- `mod(event)` \xE2\x80\x94 only needs the current event.\n"
           "- `mod(ctx, tag)` \xE2\x80\x94 a tag request (start, load_event, next_event, etc.).\n"
           "\n"
           "### context_action return values\n"
           "\n"
           "| Action       | Meaning                                |\n"
           "|--------------|----------------------------------------|\n"
           "| `next`       | Pass the event to the next mod.        |\n"
           "| `drop_event` | Drop this event.                       |\n"
           "| `recovery`   | Restart / enter watch mode.            |\n"
           "| `exit`       | Exit the pipeline.                     |\n"
           "\n"
           "### Key invariants\n"
           "\n"
           "- Mods derive from `consteval_copyable`: runtime copies abort.\n"
           "- Every mod must be `nothrow`-invocable.\n"
           "- `pimpl_idiom`-based mods allocate lazily at `start`; handlers must outlive the pipeline.\n";

    out += "\n";
    out += "## Example: header-only mod (stopper.ixx)\n";
    embed_file(out, err, root / "mods/stopper.ixx", "mods/stopper.ixx", head_lines);

    out += "\n";
    out += "## Example: mod with state + .cxx (scale.ixx)\n";
    embed_file(out, err, root / "mods/scale.ixx", "mods/scale.ixx", head_lines);

    out += "\n";
    out += "## Example: .cxx implementation (scale.cxx)\n";
    embed_file(out, err, root / "mods/scale.cxx", "mods/scale.cxx", head_lines);

    out += "\n";
    out += "## Example: mod with pimpl (io_manager.ixx header)\n";
    embed_file(out, err, root / "mods/io_manager.ixx", "mods/io_manager.ixx", head_lines);

    out += "\n";
    out += "## Registration: mods/mods.ixx (export imports)\n";
    embed_file(out, err, root / "mods/mods.ixx", "mods/mods.ixx", head_lines);

    return {std::move(out), 0, false, std::move(err)};
}

void render_help_fs8_mod(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt fs8.mod [--head N]
       prompt fs8.mod | prompt note "Create a mod that ..."

Builds a prompt for writing a new pipeline mod for the foresight project.
Must be run from inside the foresight git repo (or a subdirectory of it).

The prompt embeds AGENTS.md, a mod-writing guide, and example mod files
so the AI has enough context to generate correct, buildable code.

Options:
  --head N   Keep only the first N lines of each embedded context file
)EOF";
}

} // namespace prompt::prompts
