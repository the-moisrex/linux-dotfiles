#pragma once

// Tool input declarations, generalized for this dispatcher.
//
// opencode describes each tool's input with an annotated `Schema.Struct` and
// hangs it off the tool registration. The same idea, ported: a prompt declares
// a `schema::tool` (a name, a summary and typed parameters/operands) and the
// declaration is stored on its `prompt_descriptor` — there is no second
// registry. The declaration drives the prompt's `Options:` help block, its
// completion candidates, the AI-facing JSON Schema `.tools` emits, and the
// `schema::parse` step that turns a model's JSON tool call into argv.
//
// `from_help` remains a best-effort fallback for prompts that have not declared
// one yet. It is catalog-only: it can describe a prompt, but it cannot be the
// execution contract of a tool call (see `sdk/tool_call.hpp`).

#include "completion.hpp"
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::schema {

// The parameter types the CLI can express. Strings and value flags dominate;
// booleans are value-less flags.
enum class type : std::uint8_t { boolean, string, integer };

[[nodiscard]] std::string_view to_string(type value);

struct parameter {
    std::string name; // long flag without dashes ("head"); the flag is `--head`
    std::string description;
    type kind = type::boolean;
    bool required = false;
    bool takes_value = false;             // false: a value-less flag
    bool repeatable = false;              // true: the flag may appear several times
    std::vector<std::string> enum_values; // non-empty: the allowed values
    std::string value_placeholder;        // "N", "MODE", ... for help rendering
    std::string alias;                    // short flag without dashes ("h"); "" for none
};

// A documented positional operand (a FILE, a PATTERN, ...). Schemas keep these
// for the AI-facing description; the CLI parse lives in the prompt itself.
struct operand {
    std::string name;
    std::string description;
    bool required = false;
    bool variadic = false;
};

struct tool {
    std::string name;
    std::string summary;
    std::vector<parameter> parameters;
    std::vector<operand> operands;
};

// Factories: keep prompt files free of aggregate-initialization noise (and the
// `-Wmissing-field-initializers` warnings that come with it).
[[nodiscard]] parameter bool_flag(std::string name, std::string description, std::string alias = {});
[[nodiscard]] parameter str_flag(std::string name, std::string description, std::string placeholder = "VALUE",
                                 std::string alias = {});
[[nodiscard]] parameter int_flag(std::string name, std::string description, std::string alias = {});
[[nodiscard]] parameter enum_flag(std::string name, std::string description, std::vector<std::string> values,
                                  std::string alias = {});
// A value flag that may be repeated (`--include a --include b`).
[[nodiscard]] parameter multi_flag(std::string name, std::string description, std::string placeholder = "VALUE",
                                   std::string alias = {});
[[nodiscard]] operand argument(std::string name, std::string description, bool required = false, bool variadic = false);
[[nodiscard]] tool make_tool(std::string name, std::string summary, std::vector<parameter> parameters,
                             std::vector<operand> operands = {});

// Derive a tool from a prompt's help text: the `Options:` block supplies the
// flags (a `N`/`MODE`-style placeholder means the flag takes a value). Used for
// prompts that have not declared a schema; catalog-only, never executable.
[[nodiscard]] tool from_help(std::string_view name, std::string_view summary, std::string_view help);

// Renderers. `to_json` is implemented in `sdk/tool_call.cpp` (the JSON-aware
// translation unit), so this header stays free of nlohmann.
[[nodiscard]] std::string to_json(tool const& value);     // pretty JSON Schema
[[nodiscard]] std::string to_markdown(tool const& value); // an AI-facing description
[[nodiscard]] std::string render_options(tool const& value, std::string_view extra = {});

// Completion from the schema: the flags and, after a value flag, its enum
// values. A prompt's own `complete_fn` still adds positional candidates.
void complete(tool const& value, completion_request const& req, std::ostream& os);

} // namespace prompt::schema
