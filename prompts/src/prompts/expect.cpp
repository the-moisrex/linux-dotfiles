#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/stage.hpp"
#include <chrono>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

static void render_help_expect(std::ostream& os);

namespace {

[[nodiscard]] std::optional<std::filesystem::path> git_root_of(view const& v) {
    if (v.env.git_root.empty()) return std::nullopt;
    return v.env.git_root;
}

} // namespace

static void execute_expect(view const& v, result& r) {
    std::string selector;
    for (auto arg : v.args) {
        if (arg.starts_with('-')) {
            throw usage_error{std::format("prompt expect: unknown option {}\n{}", arg, help_text(render_help_expect))};
        }
        if (!selector.empty())
            throw usage_error{"prompt expect: expected exactly one selector\n" + help_text(render_help_expect)};
        selector = std::string(arg);
    }
    if (selector.empty()) {
        throw usage_error{"prompt expect: a selector is required (e.g. table:symbol, command:prompt, none)\n" +
                          help_text(render_help_expect)};
    }

    auto const tool = fs::bin_tool("md.extract", v.env.exe_path, git_root_of(v));
    auto const result = process::command{std::vector<std::string>{"python3", tool.string(), "--contract", selector}}
                            .capture_stderr()
                            .timeout(std::chrono::seconds(30))
                            .run();
    if (result.exit_code != 0) {
        std::string message = result.stderr_data;
        if (message.empty()) message = std::format("prompt expect: md.extract failed for {}\n", selector);
        if (result.exit_code == 2) throw usage_error{std::move(message)};
        throw soft_error{std::move(message)};
    }

    r.out = result.stdout_data;
    r.published.set(keys::expect, selector);
}

static void render_help_expect(std::ostream& os) {
    os << R"EOF(Usage: prompt <prompts...> .expect SELECTOR <ai sink>

State the shape of the answer the AI should return, and remember it for the
extraction stage. The instruction is appended to the chain context (so the next
sink sees it) and the selector is published as a fact, so `.extract`, `.each`
and `.exec` need no selector of their own. The sink replaces the context with
the answer, so the contract itself never leaks into later prompts.

SELECTOR is the shared `md.extract` grammar:

  table[:col1,col2]   one Markdown table (optionally projected columns)
  code[:lang]         one fenced code block, optionally in that language
  section[:heading]   one Markdown section
  command[:program]   one fenced shell block with a `program` command line
  frontmatter         a leading YAML/TOML frontmatter block
  none                plain prose, no fences/tables/headings
  auto                let the extractor choose the only table/code/section
  @N                  the Nth match (e.g. table:symbol@2)

Examples:
  prompt .note "screen TSE banks" .expect table:symbol,reason .ai.dseek .extract table:symbol
  prompt .delegate "fix the test" .ai.dseek .extract .exec

Options:
  -h, --help   Show this help.
)EOF";
}

bool const registered =
    prompt::register_prompt("expect",
                            "State the exact response shape the AI should return and publish it for the following "
                            "extraction stage.",
                            &execute_expect, &render_help_expect);

} // namespace prompt::prompts
