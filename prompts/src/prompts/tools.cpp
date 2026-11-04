#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/schema.hpp"
#include "../sdk/stage.hpp"
#include "../sdk/tool_call.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

enum class format : std::uint8_t { markdown, json, names };
enum class audience : std::uint8_t { human, chat };

struct tools_options {
    format how = format::markdown;
    audience who = audience::human;
    bool format_explicit = false;
    bool compact = false;
    bool all_categories = true;
    std::vector<prompt_category> categories;
};

std::optional<prompt_category> parse_category(std::string_view name) {
    if (name == "core") return prompt_category::core;
    if (name == "ai") return prompt_category::ai;
    if (name == "history") return prompt_category::history;
    if (name == "catalog") return prompt_category::catalog;
    if (name == "code") return prompt_category::code;
    if (name == "git") return prompt_category::git;
    if (name == "tse") return prompt_category::tse;
    if (name == "other") return prompt_category::other;
    return std::nullopt;
}

void add_categories(tools_options& opts, std::string_view list) {
    if (list == "all") {
        opts.all_categories = true;
        opts.categories.clear();
        return;
    }
    opts.all_categories = false;
    std::size_t begin = 0;
    while (begin <= list.size()) {
        auto const end = list.find(',', begin);
        auto const part = list.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        auto const category = parse_category(part);
        if (!category.has_value()) throw usage_error{"tools: unknown category '" + std::string(part) + "'\n"};
        opts.categories.push_back(*category);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
}

tools_options parse_args(view const& v) {
    tools_options opts;
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--json") {
            opts.how = format::json;
            opts.format_explicit = true;
        } else if (arg == "--markdown") {
            opts.how = format::markdown;
            opts.format_explicit = true;
        } else if (arg == "--names") {
            opts.how = format::names;
            opts.format_explicit = true;
        } else if (arg == "--format") {
            if (++i >= v.args.size()) throw usage_error{"tools: --format needs markdown, json or names\n"};
            std::string_view const value = v.args[i];
            if (value == "json") {
                opts.how = format::json;
            } else if (value == "markdown") {
                opts.how = format::markdown;
            } else if (value == "names") {
                opts.how = format::names;
            } else {
                throw usage_error{"tools: unknown format '" + std::string(value) + "'\n"};
            }
            opts.format_explicit = true;
        } else if (arg.starts_with("--format=")) {
            std::string_view const value = arg.substr(std::string_view{"--format="}.size());
            if (value == "json") {
                opts.how = format::json;
            } else if (value == "markdown") {
                opts.how = format::markdown;
            } else if (value == "names") {
                opts.how = format::names;
            } else {
                throw usage_error{"tools: unknown format '" + std::string(value) + "'\n"};
            }
            opts.format_explicit = true;
        } else if (arg == "--compact") {
            opts.compact = true;
        } else if (arg == "--for") {
            if (++i >= v.args.size()) throw usage_error{"tools: --for needs human or chat\n"};
            std::string_view const value = v.args[i];
            if (value == "human") {
                opts.who = audience::human;
            } else if (value == "chat") {
                opts.who = audience::chat;
            } else {
                throw usage_error{"tools: unknown audience '" + std::string(value) + "'\n"};
            }
        } else if (arg == "--category") {
            if (++i >= v.args.size()) throw usage_error{"tools: --category needs a name\n"};
            add_categories(opts, v.args[i]);
        } else if (arg.starts_with("--category=")) {
            add_categories(opts, arg.substr(std::string_view{"--category="}.size()));
        } else {
            throw usage_error{"tools: unknown option " + std::string(arg)};
        }
    }
    // The chat face is a JSON catalog plus the calling protocol; an explicit
    // --format still wins for a human-readable preview.
    if (opts.who == audience::chat && !opts.format_explicit) opts.how = format::json;
    return opts;
}

bool selected(tools_options const& opts, prompt_descriptor const& desc) {
    if (opts.all_categories) return true;
    return std::ranges::find(opts.categories, desc.category) != opts.categories.end();
}

// The tool schema for one registered prompt: its declared schema when it has
// one, otherwise a best-effort one derived from its own help text.
schema::tool tool_for(prompt_descriptor const& desc) {
    if (desc.input != nullptr) return *desc.input;
    std::string help;
    if (desc.render_help_fn != nullptr) help = help_text(desc.render_help_fn);
    return schema::from_help(desc.name, desc.help_summary, help);
}

nlohmann::ordered_json tool_object(prompt_descriptor const& desc) {
    nlohmann::ordered_json obj;
    obj["name"] = desc.name;
    obj["description"] = desc.help_summary;
    obj["category"] = to_string(desc.category);
    obj["input"] = schema::to_json_value(tool_for(desc))["input"];
    return obj;
}

std::string render_chat_preamble() {
    return R"EOF(# Tools

You can call the following tools. To call one, emit a fenced ```json block
containing a single object:

{"tool": "<name>", "input": {<parameters>}}

Batch independent calls into the SAME answer: when you need several files,
several searches, or several commands, emit one block per call together instead
of asking for them one at a time. They all run in this turn and you get every
result back together, so batching saves round trips. Split calls across turns
only when one depends on another's result.

For example, to read two files at once:

```json
{"tool": "read", "input": {"file": ["src/a.cpp", "src/b.cpp"]}}
```

After the calls run you will see a `### Tool: <name>` result for each; then
answer the user or call more tools. If no tool is needed, answer directly
without a tool block.

## Available tools

)EOF";
}

} // namespace

static void execute_tools(view const& v, result& r) {
    tools_options const opts = parse_args(v);

    std::vector<prompt_descriptor const*> prompts;
    for (auto const& desc : get_all_prompts()) {
        if (!selected(opts, desc)) continue;
        // The chat face only advertises callable tools: a prompt without an
        // input schema cannot be executed from a JSON call.
        if (opts.who == audience::chat && desc.input == nullptr) continue;
        prompts.push_back(&desc);
    }
    std::sort(prompts.begin(), prompts.end(), prompt_descriptor_less);

    std::string out;
    if (opts.who == audience::chat) out += render_chat_preamble();

    if (opts.how == format::json) {
        nlohmann::ordered_json array = nlohmann::ordered_json::array();
        for (auto const* desc : prompts) array.push_back(tool_object(*desc));
        out += array.dump(2);
        out += '\n';
    } else if (opts.how == format::names) {
        for (auto const* desc : prompts) {
            out += desc->name;
            out += '\n';
        }
    } else if (opts.compact) {
        for (auto const* desc : prompts) {
            out += desc->name;
            out += " [";
            out += to_string(desc->category);
            out += "] ";
            out += desc->help_summary;
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
    os << R"EOF(Usage: prompt tools [--category NAME[,NAME...]] [--compact]
                    [--for human|chat] [--format markdown|json|names]

Describe the prompt catalog as tool schemas, the way opencode describes its
tools to a model: a name, a one-line summary, typed options and operands. A
prompt that declares an input schema (`prompt_descriptor::input`) is reported
exactly; every other prompt gets a schema derived from its own `Options:` help
block (catalog-only: it cannot be executed as a tool).

This is the machine-readable face of `.list`/`.list-prompts`, and the catalog an
agentic chain reads before composing a `prompt ...` command or a JSON tool call.

Options:
  --category NAME[,NAME...]  Only these categories (default: all). Names: core,
                             ai, history, catalog, code, git, tse, other.
  --compact                  One line per prompt: name [category] summary
  --for human                Descriptions only (default)
  --for chat                 Callable tools plus the JSON calling protocol
  --format markdown          Human/AI-readable descriptions (default)
  --format json              A JSON array of tool objects
  --format names             Just the prompt names, one per line
  -h, --help                 Show this help message

Examples:
  prompt tools --compact --category core
  prompt tools --for chat --category core,code
  prompt tools --json --category tse | jq '.[].name'
  prompt tools --names | prompt .extract

See also: `.list` (short descriptions), `.list-prompts` (full help),
`.tools.run` (execute the tool calls in an AI answer), and `.compress.list`.

)EOF";
}

bool const registered_tools =
    prompt::register_prompt("tools", "Describe the prompt catalog as tool schemas (markdown, JSON Schema or names).",
                            &execute_tools, &render_help_tools);

} // namespace prompt::prompts
