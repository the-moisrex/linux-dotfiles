#include "../sdk/extract.hpp"
#include "../extract/render.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include <algorithm>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::prompts {

static void render_help_extract(std::ostream& os);

namespace {

struct select_flags {
    bool json = false;
    bool list = false;
    bool pick = false;
};

// A published binding value never carries a trailing newline (the shape the
// former `md.extract --select --one` output had).
[[nodiscard]] std::string rstrip_newlines(std::string_view text) {
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return std::string(text);
}

[[noreturn]] void throw_no_match(extract::selection const& sel) {
    throw soft_error{std::format("prompt extract: no matching {} blocks (try --list, --kind any, or --guess)\n",
                                 extract::selector_kind_names(sel))};
}

} // namespace

static void execute_extract(view const& v, result& r) {
    std::vector<extract::selection> entries;
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
        for (auto& entry : extract::parse_plan(arg)) entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        if (auto expected = v.prior.get(keys::expect)) entries = extract::parse_plan(*expected);
    }
    // `--list` is the inventory path: show every candidate instead of making
    // the ambiguous `auto` choice.
    if (entries.empty()) entries.push_back(extract::parse_selection(flags.list ? "any" : "auto"));

    auto const primaries = std::count_if(entries.begin(), entries.end(),
                                         [](extract::selection const& entry) { return entry.bind.empty(); });
    if (primaries == 0 && entries.size() > 1) {
        throw usage_error{"prompt extract: the plan needs an unnamed (primary) selector\n" +
                          help_text(render_help_extract)};
    }

    if (v.text.empty()) throw soft_error{"prompt extract: no accumulated context to extract from\n"};

    // Named entries are extracted first, each requiring exactly one match, and
    // published as their fact.
    for (auto const& entry : entries) {
        if (entry.bind.empty()) continue;
        auto const matches = extract::run(v.text, entry);
        if (matches.size() != 1) {
            if (matches.empty()) throw soft_error{"prompt extract: no block matches --one\n"};
            throw usage_error{std::format(
                "prompt extract: --one needs exactly one block, got {} (narrow the selection or drop --one)\n",
                matches.size())};
        }
        r.published.set(entry.bind, rstrip_newlines(extract::render_selection(matches, entry, false, {})));
    }

    // The primary entry replaces the chain context (a named-only single entry
    // acts as the primary too, matching `.expect`).
    extract::selection const& primary =
        primaries == 1
            ? *std::ranges::find_if(entries, [](extract::selection const& entry) { return entry.bind.empty(); })
            : entries.front();
    auto matches = extract::run(v.text, primary);

    if (flags.list) {
        // Inventory only: show the candidates on stderr and leave the chain
        // context untouched (an empty sink output is a passthrough).
        if (matches.empty()) throw_no_match(primary);
        r.err = extract::render_list(matches, false);
        r.no_clipboard = true;
        return;
    }

    if (matches.empty()) throw_no_match(primary);
    if (flags.pick) matches = extract::pick(matches, false);

    if (flags.json) {
        auto array = nlohmann::ordered_json::array();
        for (auto const& m : matches) array.push_back(extract::block_record(m, false));
        r.out = array.dump(2) + "\n";
        return;
    }

    r.out = extract::render_selection(matches, primary, false, {});
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
    &execute_extract, &render_help_extract, &extract::complete_selector);

} // namespace prompt::prompts
