#include "prompt/prompts/fs8_test_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>
#include <string_view>
#include <system_error>

namespace prompt::prompts {

namespace {

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

prompt_result execute_fs8_test(prompt_context&& ctx) noexcept {
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

    out += "You are an expert C++26 developer writing GoogleTest tests for a foresight pipeline mod.\n";
    out += "\n";
    out += "Tests live in tests/ as *_test.cxx files. Each file is a standalone test suite.\n";
    out += "Tests are only built in Debug mode. The CMakeLists.txt auto-discovers all *_test.cxx files.\n";
    out += "\n";
    out += "Your task: generate a complete, correct test file that follows all project conventions.\n";
    out += "\n";

    out += "## Build/test reference\n";
    out += "\n";
    out += "To build and run tests:\n"
           "\n"
           "    cmake --preset debug-gcc\n"
           "    cmake --build --preset debug-gcc\n"
           "    ctest --test-dir build-debug-gcc/tests -R test-<name>\n"
           "\n"
           "Or run a single test binary directly:\n"
           "\n"
           "    ./build-debug-gcc/tests/test-<name>\n";

    out += "\n";
    out += "## Test patterns\n";
    out += "\n";
    out += "Every test file follows this structure:\n"
           "\n"
           "1. First include (before any import):\n"
           "   #include \"common/tests_common_pch.hpp\"\n"
           "\n"
           "2. Then system headers, then module import:\n"
           "   #include <linux/input-event-codes.h>\n"
           "   import fs8.mods;\n"
           "\n"
           "3. Basic test with emit_all + record (most common pattern):\n"
           "   - Build a pipeline: context | emit_all[{...}] | <mod-under-test> | record\n"
           "   - Get a reference: auto& col = pipeline.mod<basic_record>();\n"
           "   - Run: pipeline();\n"
           "   - Check results: col.without_syn() returns non-SYN events\n"
           "\n"
           "4. The emit_all initializer list uses this format:\n"
           "   {EV_ABS, ABS_X, 1000},  // {type, code, value}\n"
           "   {EV_SYN, SYN_REPORT, 0},\n"
           "\n"
           "5. For time-dependent tests (like debounce):\n"
           "   - Use a timed_sequence custom load_event provider with explicit timestamps\n"
           "   - Define a consteval timed_ev() helper for event construction\n"
           "   - See debounce_test.cxx for the full pattern\n"
           "\n"
           "6. Common assertions:\n"
           "   - ASSERT_EQ / EXPECT_EQ for values\n"
           "   - ASSERT_EQ(events.size(), NU) for event counts\n"
           "   - Check event.type(), event.code(), event.value()\n"
           "\n"
           "7. The record mod:\n"
           "   - record captures all events that pass through\n"
           "   - col.without_syn() returns events excluding SYN_REPORT\n"
           "   - col.size() gives total event count\n"
           "   - col[i] provides indexed access\n";

    out += "\n";
    out += "## Example test file (scale_test.cxx)\n";
    embed_file(out, err, root / "tests/scale_test.cxx", "tests/scale_test.cxx", head_lines);

    out += "\n";
    out += "## Example: time-dependent test (debounce_test.cxx)\n";
    embed_file(out, err, root / "tests/debounce_test.cxx", "tests/debounce_test.cxx", head_lines);

    out += "\n";
    out += "## Mod source being tested (for reference)\n";
    embed_file(out, err, root / "mods/mods.ixx", "mods/mods.ixx", head_lines);

    return {std::move(out), 0, false, std::move(err)};
}

void render_help_fs8_test(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt fs8.test [--head N]
       prompt fs8.test | prompt note "Write tests for ..."

Builds a prompt for writing GoogleTest tests for a foresight pipeline mod.
Must be run from inside the foresight git repo (or a subdirectory of it).

The prompt embeds the test infrastructure, patterns, and example test files
so the AI generates tests that follow the project's conventions.

Options:
  --head N   Keep only the first N lines of each embedded context file
)EOF";
}

} // namespace prompt::prompts
