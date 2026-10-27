#include "../sdk/errors.hpp"
#include "../sdk/extract.hpp"
#include "common.hpp"
#include "render.hpp"
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::vector<match> run_field(std::string_view text, std::string_view qualifier) {
    auto const label = trim_view(qualifier);
    if (label.empty()) throw usage_error{"prompt: the field selector needs a label: field:filename\n"};
    std::vector<match> out;
    for (auto const& hit : find_fields(text, label)) {
        match m;
        m.kind = "field";
        m.title = std::string(label);
        m.text = hit.value;
        m.source = hit.value;
        m.line = hit.line;
        m.end_line = hit.line;
        out.push_back(std::move(m));
    }
    return out;
}

contract_text contract_field(std::string_view qualifier) {
    std::string const label = trim_view(qualifier).empty() ? std::string{"value"} : std::string(trim_view(qualifier));
    return make_contract(std::format("Also include a line `{}: <value>` carrying the requested value, "
                                     "on a line of its own.\n",
                                     label),
                         std::format("a line `{}: <value>` carrying the requested value", label));
}

void complete_field(std::string_view prefix, std::ostream& os) {
    for (auto const* label : {"filename", "name", "title", "command", "summary"}) {
        if (completion::matches(prefix, label)) completion::write_item(os, label, "field label");
    }
}

} // namespace

bool const registered_field = register_extractor(extractor{
    .name = "field",
    .summary = "a `label: value` line; field:label reads that value",
    .run = &run_field,
    .contract = &contract_field,
    .complete = &complete_field,
});

} // namespace prompt::extract
