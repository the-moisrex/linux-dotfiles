#include "../sdk/errors.hpp"
#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <format>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::vector<match> run_section(std::string_view text, std::string_view qualifier) {
    auto blocks = parse_document(text, "");
    assign_indices(blocks);

    std::optional<std::regex> pattern;
    if (!trim_view(qualifier).empty()) {
        try {
            pattern.emplace(std::string(qualifier), std::regex::icase | std::regex::ECMAScript);
        } catch (std::regex_error const& error) {
            throw usage_error{std::format("prompt: bad section pattern `{}`: {}\n", qualifier, error.what())};
        }
    }

    std::vector<match> out;
    for (auto const& b : blocks) {
        if (b.kind != "section") continue;
        if (pattern.has_value() && !std::regex_search(b.title, *pattern)) continue;
        out.push_back(match_from(b));
    }
    return out;
}

contract_text contract_section(std::string_view qualifier) {
    std::string const heading = trim_view(qualifier).empty() ? "Answer" : std::string(trim_view(qualifier));
    return make_contract(std::format("Use a Markdown section headed `{}` and put the whole answer under it. "
                                     "Do not add other top-level sections.\n",
                                     heading),
                         std::format("a Markdown section headed `{}` containing the answer", heading));
}

} // namespace

bool const registered_section = register_extractor(extractor{
    .name = "section",
    .summary = "a heading and everything under it; section:heading matches the heading",
    .run = &run_section,
    .contract = &contract_section,
});

} // namespace prompt::extract
