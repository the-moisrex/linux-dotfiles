#include "../sdk/errors.hpp"
#include "../sdk/extract.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/stage.hpp"
#include <algorithm>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

static void render_help_expect(std::ostream& os);

static void execute_expect(view const& v, result& r) {
    std::vector<extract::selection> entries;
    for (auto arg : v.args) {
        if (arg.starts_with('-')) {
            throw usage_error{std::format("prompt expect: unknown option {}\n{}", arg, help_text(render_help_expect))};
        }
        auto parsed = extract::parse_plan(arg);
        if (parsed.empty()) {
            throw usage_error{std::format("prompt expect: empty selector {}\n{}", arg, help_text(render_help_expect))};
        }
        for (auto& entry : parsed) entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        throw usage_error{"prompt expect: a selector is required (e.g. table:symbol, command:prompt, none)\n" +
                          help_text(render_help_expect)};
    }
    // The primary (unnamed) entry is the shape that replaces the context; the
    // named entries are extra values `.extract` publishes as facts.
    auto const primaries = std::count_if(entries.begin(), entries.end(),
                                         [](extract::selection const& entry) { return entry.bind.empty(); });
    if (primaries != 1) {
        throw usage_error{"prompt expect: the plan needs exactly one unnamed (primary) selector\n" +
                          help_text(render_help_expect)};
    }
    std::stable_sort(entries.begin(), entries.end(), [](extract::selection const& a, extract::selection const& b) {
        return a.bind.empty() && !b.bind.empty();
    });

    r.out = entries.size() == 1 ? extract::contract_for(entries.front()) : extract::combined_contract(entries);
    r.published.set(keys::expect, extract::format_plan(entries));
}

static void render_help_expect(std::ostream& os) {
    os << R"EOF(Usage: prompt <prompts...> .expect [NAME=]SELECTOR [[NAME=]SELECTOR...] <ai sink>

State the shape of the answer the AI should return, and remember it for the
extraction stage. The instruction is appended to the chain context (so the next
sink sees it) and the plan is published as a fact, so `.extract`, `.each` and
`.exec` need no selector of their own. The sink replaces the context with the
answer, so the contract itself never leaks into later prompts.

Exactly one selector is the unnamed PRIMARY: its result replaces the chain
context. Any `NAME=SELECTOR` entries are extra values, extracted at the same
time and published as the fact NAME (`save.stem=field:filename` gives `.save`
its file name). `.each` and `.exec` use the primary entry only.

SELECTOR is the shared `md.extract` grammar:

  table[:col1,col2]   one Markdown table (optionally projected columns)
  code[:lang]         one fenced code block, optionally in that language
  section[:heading]   one Markdown section
  command[:program]   one fenced shell block with a `program` command line
  field:label         the value of a `label: value` line
  frontmatter         a leading YAML/TOML frontmatter block
  none                plain prose, no fences/tables/headings
  auto                let the extractor choose the only table/code/section
  @N                  the Nth match (e.g. table:symbol@2)

Examples:
  prompt .note "screen TSE banks" .expect table:symbol,reason .ai.dseek .extract
  prompt .note "fix it" .delegate .ai.dseek .extract .exec
  prompt .note "make a prompt" .delegate .ai.dseek \
    .expect command:prompt save.stem=field:filename .extract .save

Options:
  -h, --help   Show this help.
)EOF";
}

bool const registered =
    prompt::register_prompt("expect",
                            "State the exact response shape the AI should return and publish it for the following "
                            "extraction stage.",
                            &execute_expect, &render_help_expect, &extract::complete_selector);

} // namespace prompt::prompts
