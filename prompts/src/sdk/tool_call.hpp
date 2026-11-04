#pragma once

// The tool-call layer: the JSON face of a prompt's `schema::tool` declaration.
//
// opencode hands a model a tool list and receives structured `tool_calls`. The
// web providers this dispatcher talks to have no such channel, so the protocol
// is a fenced ```json block:
//
//     {"tool": "grep", "input": {"pattern": "TODO", "path": "src"}}
//
// This header owns the three steps that are provider-independent:
//
//   * `extract`   — find the calls in an AI answer,
//   * `parse`     — turn a call's `input` into the prompt's argv (the contract),
//   * `to_argv`   — resolve the tool name and parse in one step,
//   * `format_result` — the `### Tool:` block fed back to the model.
//
// `.tools.run` is a thin sink over these; a future `.agent` loop reuses them
// unchanged. JSON lives here, not in `schema.hpp`, so prompt translation units
// do not pay for nlohmann.

#include "schema.hpp"
#include "stage.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::schema {

// The JSON-Schema object `.tools` emits (and `to_json` dumps).
[[nodiscard]] nlohmann::ordered_json to_json_value(tool const& value);

// Turn a model's `input` object into argv tokens for the prompt's own parser.
// Throws usage_error on an unknown key, a wrong type, a bad enum value or a
// missing required parameter/operand. This is the execution contract: a flag
// `.tools` advertises must parse here, and an unadvertised input is rejected.
[[nodiscard]] std::vector<std::string> parse(tool const& value, nlohmann::json const& input);

} // namespace prompt::schema

namespace prompt::tool_call {

// One call found in an AI answer.
struct call {
    std::string name;
    nlohmann::json input;
    std::string block; // the fenced body the call came from
};

// Every `{"tool": …}` object in `text`: a bare JSON document, a JSON array, or
// any fenced ```json block (untagged blocks are tried too). A block that is
// valid JSON without a `tool` key is ignored, so ordinary code samples and
// `.delegate` output are never mistaken for calls.
[[nodiscard]] std::vector<call> extract(std::string_view text);

// `find_prompt(name)` + `schema::parse`. Throws usage_error when the name is
// unknown or the prompt declares no input schema (i.e. is not a tool).
[[nodiscard]] std::vector<std::string> to_argv(call const& value);

// The model-facing result block:
//
//     ### Tool: grep
//     Input: {"pattern": "TODO"}
//     Status: ok
//
//     <output>
[[nodiscard]] std::string format_result(call const& value, std::string_view output, int exit_code, bool ok);

} // namespace prompt::tool_call
