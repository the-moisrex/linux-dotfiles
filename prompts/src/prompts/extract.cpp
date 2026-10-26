#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../core/strings.hpp"
#include "../sdk/agent.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include <algorithm>
#include <chrono>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::prompts {

static void render_help_extract(std::ostream& os);

namespace {

[[nodiscard]] std::optional<std::filesystem::path> git_root_of(view const& v) {
    if (v.env.git_root.empty()) return std::nullopt;
    return v.env.git_root;
}

struct select_flags {
    bool json = false;
    bool list = false;
    bool pick = false;
};

// One `md.extract --select <selector>` run over the chain context. Extra flags
// carry the sink's own `--json`/`--list`/`--pick` for the primary, and `--one`
// for a named binding (so a name is unambiguous).
[[nodiscard]] process::command_result run_select(view const& v, std::string_view selector,
                                                 std::vector<std::string> const& extra) {
    auto const tool = fs::bin_tool("md.extract", v.env.exe_path, git_root_of(v));
    std::vector<std::string> argv{"python3", tool.string(), "--select", std::string(selector)};
    for (auto const& flag : extra) argv.push_back(flag);
    return process::command{argv}
        .stdin_data(std::string(v.text))
        .capture_stderr()
        .timeout(std::chrono::minutes(5))
        .run();
}

void throw_tool_error(process::command_result const& result, std::string_view selector) {
    std::string message = result.stderr_data;
    if (message.empty()) message = std::format("prompt extract: md.extract failed for {}\n", selector);
    if (result.exit_code == 2) throw usage_error{std::move(message)};
    throw soft_error{std::move(message)};
}

} // namespace

static void execute_extract(view const& v, result& r) {
    std::vector<agent::expect_entry> entries;
    select_flags flags;
    bool options = true;
    for (auto arg : v.args) {
        if (options && arg == "--") {
            options = false;
            continue;
        }
        if (options && arg == "--json") {
            flags.json = true;
            continue;
        }
        if (options && arg == "--list") {
            flags.list = true;
            continue;
        }
        if (options && arg == "--pick") {
            flags.pick = true;
            continue;
        }
        if (options && arg.starts_with('-')) {
            throw usage_error{
                std::format("prompt extract: unknown option {}\n{}", arg, help_text(render_help_extract))};
        }
        for (auto& entry : agent::parse_expect_plan(arg)) entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        if (auto expected = v.prior.get(keys::expect)) entries = agent::parse_expect_plan(*expected);
    }
    // `--list` is the inventory path: show every candidate instead of making
    // the ambiguous `auto` choice.
    if (entries.empty()) entries.push_back(agent::expect_entry{.selector = flags.list ? "any" : "auto"});

    auto const primaries = std::count_if(entries.begin(), entries.end(),
                                         [](agent::expect_entry const& entry) { return entry.name.empty(); });
    if (primaries == 0 && entries.size() > 1) {
        throw usage_error{"prompt extract: the plan needs an unnamed (primary) selector\n" +
                          help_text(render_help_extract)};
    }

    if (v.text.empty()) throw soft_error{"prompt extract: no accumulated context to extract from\n"};

    std::vector<std::string> primary_flags;
    if (flags.json) primary_flags.emplace_back("--json");
    if (flags.list) primary_flags.emplace_back("--list");
    if (flags.pick) primary_flags.emplace_back("--pick");

    for (auto const& entry : entries) {
        if (entry.name.empty()) continue;
        auto const result = run_select(v, entry.selector, {"--one"});
        if (result.exit_code != 0) throw_tool_error(result, entry.selector);
        r.published.set(entry.name, prompt::text::rstrip_newlines(result.stdout_data));
    }

    // The primary entry replaces the chain context (a named-only single entry
    // acts as the primary too, matching `.expect`).
    std::string_view const primary =
        primaries == 1
            ? std::ranges::find_if(entries, [](agent::expect_entry const& entry) { return entry.name.empty(); })
                  ->selector
            : entries.front().selector;
    auto const result = run_select(v, primary, primary_flags);

    if (flags.list) {
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

    if (result.exit_code != 0) throw_tool_error(result, primary);
    r.out = result.stdout_data;
}

static void render_help_extract(std::ostream& os) {
    os << R"EOF(Usage: prompt <prompts...> .extract [[NAME=]SELECTOR...] [--list] [--json] [--pick]

A barrier sink that takes pieces out of the accumulated chain context with
`bin/md.extract`. With no selector it uses the plan `.expect`/`.delegate`
published, then falls back to `auto` (the only table, code block or section in
the context).

The unnamed PRIMARY selector replaces the chain context. Each `NAME=SELECTOR`
entry is extracted at the same time (requiring exactly one match) and published
as the fact NAME, so a later `.save`, `.each` or prompt can read it:
`command:prompt save.stem=field:filename` gives `.save` the AI's file name.

SELECTOR is the shared md.extract grammar: table[:col1,col2], code[:lang],
section[:heading], command[:program], field:label, frontmatter, any, auto, and
@N for the Nth match. See `md.extract --kinds`.
)EOF";
    render_sink_help(os);
    os << R"EOF(
Extraction options:
  --list   List the primary candidates on stderr and keep the context (default: replace)
  --json   Emit md.extract's JSON block records for the primary instead of rendered text
  --pick   Choose the primary candidate with fzf

Examples:
  prompt .delegate .ai.dseek .extract .exec
  prompt .delegate .ai.dseek .extract .save
  prompt .note "make a prompt" .delegate .ai.dseek .extract command:prompt save.stem=field:filename .save

)EOF";
}

bool const registered = prompt::register_sink(
    "extract",
    "Take tables, code blocks, sections, commands or labelled values out of the chain context, using the shape the AI "
    "was asked for.",
    &execute_extract, &render_help_extract);

} // namespace prompt::prompts
