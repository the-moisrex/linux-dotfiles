#pragma once

// Tool input schemas, generalized for this dispatcher.
//
// opencode describes each tool's input with an annotated `Schema.Struct`. The
// same idea, ported: a prompt may declare a `schema::tool` — a name, a summary
// and typed parameters — and the schema then drives the prompt's `Options:`
// help block, its completion candidates, and an AI-facing JSON Schema. A prompt
// that has not declared one yet still gets a best-effort schema derived from
// its own help text (`from_help`), so the catalog is complete from day one and
// richer as prompts migrate.
//
// One file = one schema: register next to the prompt (or in the prompt's own
// file), exactly like `register_prompt` / `register_compressor`.

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

inline constexpr std::size_t tool_capacity = 128;

// Factories: keep prompt files free of aggregate-initialization noise (and the
// `-Wmissing-field-initializers` warnings that come with it).
[[nodiscard]] parameter bool_flag(std::string name, std::string description, std::string alias = {});
[[nodiscard]] parameter str_flag(std::string name, std::string description, std::string placeholder = "VALUE",
                                 std::string alias = {});
[[nodiscard]] parameter int_flag(std::string name, std::string description, std::string alias = {});
[[nodiscard]] parameter enum_flag(std::string name, std::string description, std::vector<std::string> values,
                                  std::string alias = {});
[[nodiscard]] operand argument(std::string name, std::string description, bool required = false, bool variadic = false);
[[nodiscard]] tool make_tool(std::string name, std::string summary, std::vector<parameter> parameters,
                             std::vector<operand> operands = {});

// Register a tool schema. Like the prompt/extractor registries this aborts when
// the fixed capacity is exceeded, so a schema is never silently dropped.
bool register_tool(tool value) noexcept;
[[nodiscard]] tool const* find_tool(std::string_view name);
[[nodiscard]] std::span<tool const> all_tools();

// Derive a tool from a prompt's help text: the `Options:` block supplies the
// flags (a `N`/`MODE`-style placeholder means the flag takes a value). Used for
// prompts that have not declared a schema.
[[nodiscard]] tool from_help(std::string_view name, std::string_view summary, std::string_view help);

// Renderers.
[[nodiscard]] std::string to_json(tool const& value);     // pretty JSON Schema
[[nodiscard]] std::string to_markdown(tool const& value); // an AI-facing description
[[nodiscard]] std::string render_options(tool const& value, std::string_view extra = {});

// Completion from the schema: the flags and, after a value flag, its enum
// values. A prompt's own `complete_fn` still adds positional candidates.
void complete(tool const& value, completion_request const& req, std::ostream& os);

} // namespace prompt::schema
