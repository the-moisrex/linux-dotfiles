#include "../sdk/errors.hpp"
#include "../sdk/extract.hpp"
#include "../sdk/tool_call.hpp"
#include "common.hpp"
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::vector<match> run_toolcall(std::string_view text, std::string_view qualifier) {
    (void)qualifier;
    auto const calls = tool_call::extract(text);
    std::vector<match> out;
    out.reserve(calls.size());
    for (auto const& call : calls) {
        match found;
        found.kind = "toolcall";
        found.lang = "json";
        found.title = call.name;
        found.program = call.name;
        found.text = call.input.dump();
        found.source = call.block;
        out.push_back(std::move(found));
    }
    if (out.empty()) throw soft_error{"no tool calls found\n"};
    return out;
}

contract_text contract_toolcall(std::string_view qualifier) {
    (void)qualifier;
    return make_contract("Emit one fenced `json` code block per tool call, each containing a single object "
                         "`{\"tool\": \"<name>\", \"input\": {...}}`. Put no prose inside the block.\n",
                         "one fenced `json` tool call per action");
}

} // namespace

bool const registered_toolcall = register_extractor(extractor{
    .name = "toolcall",
    .summary = "a tool call from an AI answer (a fenced json block with a `tool` key)",
    .run = &run_toolcall,
    .contract = &contract_toolcall,
});

} // namespace prompt::extract
