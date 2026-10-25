#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include <chrono>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

static void render_help_extract(std::ostream& os);

namespace {

[[nodiscard]] std::optional<std::filesystem::path> git_root_of(view const& v) {
    if (v.env.git_root.empty()) return std::nullopt;
    return v.env.git_root;
}

} // namespace

static void execute_extract(view const& v, result& r) {
    std::string selector;
    bool json = false;
    bool list = false;
    bool pick = false;
    for (auto arg : v.args) {
        if (arg == "--json") {
            json = true;
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--pick") {
            pick = true;
        } else if (arg.starts_with('-')) {
            throw usage_error{
                std::format("prompt extract: unknown option {}\n{}", arg, help_text(render_help_extract))};
        } else if (!selector.empty()) {
            throw usage_error{"prompt extract: expected at most one selector\n" + help_text(render_help_extract)};
        } else {
            selector = std::string(arg);
        }
    }
    if (selector.empty()) {
        if (auto expected = v.prior.get(keys::expect)) selector = *expected;
    }
    // `--list` is the inventory path: show every candidate instead of making
    // the ambiguous `auto` choice.
    if (selector.empty()) selector = list ? "any" : "auto";

    if (v.text.empty()) throw soft_error{"prompt extract: no accumulated context to extract from\n"};

    auto const tool = fs::bin_tool("md.extract", v.env.exe_path, git_root_of(v));
    std::vector<std::string> argv{"python3", tool.string(), "--select", selector};
    if (json) argv.emplace_back("--json");
    if (list) argv.emplace_back("--list");
    if (pick) argv.emplace_back("--pick");

    auto const result =
        process::command{argv}.stdin_data(std::string(v.text)).capture_stderr().timeout(std::chrono::minutes(5)).run();

    if (list) {
        // Inventory only: show the candidates on stderr and leave the chain
        // context untouched (an empty sink output is a passthrough).
        r.err = result.stderr_data + result.stdout_data;
        r.no_clipboard = true;
        if (result.exit_code != 0) {
            if (result.exit_code == 2) throw usage_error{result.stderr_data};
            throw soft_error{result.stderr_data};
        }
        return;
    }

    if (result.exit_code != 0) {
        std::string message = result.stderr_data;
        if (message.empty()) message = std::format("prompt extract: md.extract failed for {}\n", selector);
        if (result.exit_code == 2) throw usage_error{std::move(message)};
        throw soft_error{std::move(message)};
    }
    r.out = result.stdout_data;
}

static void render_help_extract(std::ostream& os) {
    os << R"EOF(Usage: prompt <prompts...> .extract [SELECTOR] [--list] [--json] [--pick]

A barrier sink that takes one piece out of the accumulated chain context with
`bin/md.extract` and replaces the context with it. Without a selector it uses
the shape `.expect`/`.delegate` published, then falls back to `auto` (the only table, code block or
section in the context).

SELECTOR is the shared md.extract grammar: table[:col1,col2], code[:lang],
section[:heading], command[:program], frontmatter, any, auto, and @N for the
Nth match. See `md.extract --kinds`.
)EOF";
    render_sink_help(os);
    os << R"EOF(
Extraction options:
  --list   List the candidates on stderr and keep the context (default: replace)
  --json   Emit md.extract's JSON block records instead of rendered text
  --pick   Choose one candidate with fzf

)EOF";
}

bool const registered = prompt::register_sink(
    "extract",
    "Take one table, code block, section or command out of the chain context, using the shape the AI was asked "
    "for.",
    &execute_extract, &render_help_extract);

} // namespace prompt::prompts
