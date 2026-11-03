#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/schema.hpp"
#include "../sdk/stage.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

// The tool schema for one registered prompt: its declared schema when it has
// one, otherwise a best-effort one derived from its own help text.
schema::tool tool_for(prompt_descriptor const& desc) {
    if (schema::tool const* declared = schema::find_tool(desc.name); declared != nullptr) return *declared;
    std::string help;
    if (desc.render_help_fn != nullptr) help = help_text(desc.render_help_fn);
    return schema::from_help(desc.name, desc.help_summary, help);
}

} // namespace

static void execute_tools(view const& v, result& r) {
    enum class format : std::uint8_t { markdown, json, names };
    format how = format::markdown;
    for (std::string_view const arg : v.args) {
        if (arg == "--json") {
            how = format::json;
        } else if (arg == "--names") {
            how = format::names;
        } else if (arg == "--markdown") {
            how = format::markdown;
        } else if (arg == "-h" || arg == "--help") {
            throw usage_error{"tools: see prompt list-prompts for the catalog help"};
        } else {
            throw usage_error{"tools: unknown option " + std::string(arg)};
        }
    }

    std::vector<prompt_descriptor const*> prompts;
    for (auto const& desc : get_all_prompts()) prompts.push_back(&desc);
    std::sort(prompts.begin(), prompts.end(), prompt_descriptor_less);

    std::string out;
    if (how == format::json) {
        out += "[\n";
        for (std::size_t i = 0; i < prompts.size(); ++i) {
            out += schema::to_json(tool_for(*prompts[i]));
            if (i + 1 < prompts.size()) out += ',';
            out += '\n';
        }
        out += "]\n";
    } else if (how == format::names) {
        for (auto const* desc : prompts) {
            out += desc->name;
            out += '\n';
        }
    } else {
        for (auto const* desc : prompts) {
            out += schema::to_markdown(tool_for(*desc));
            out += '\n';
        }
    }
    r.out = std::move(out);
}

static void render_help_tools(std::ostream& os) {
    os << R"EOF(Usage: prompt tools [--json|--markdown|--names]

Describe the whole prompt catalog as tool schemas, the way opencode describes
its tools to a model: a name, a one-line summary, typed options and operands.
A prompt that declares a schema (see `sdk/schema.hpp`) is reported exactly;
every other prompt gets a schema derived from its own `Options:` help block.

This is the machine-readable face of `.list`/`.list-prompts`, and the catalog an
agentic chain reads before composing a `prompt …` command.

Options:
  --markdown   Human/AI-readable descriptions (default)
  --json       A JSON array of JSON-Schema tool objects
  --names      Just the prompt names, one per line
  -h, --help   Show this help message

Examples:
  prompt tools
  prompt tools --json | jq '.[].name'
  prompt tools --names | prompt .extract

See also: `.list` (short descriptions), `.list-prompts` (full help), and
`.compress.list`.

)EOF";
}

bool const registered_tools =
    prompt::register_prompt("tools", "Describe every prompt as a tool schema (markdown, JSON Schema or names).",
                            &execute_tools, &render_help_tools);

} // namespace prompt::prompts
