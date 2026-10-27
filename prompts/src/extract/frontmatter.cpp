#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::vector<match> run_frontmatter(std::string_view text, std::string_view qualifier) {
    (void)qualifier;
    auto blocks = parse_document(text, "");
    assign_indices(blocks);
    std::vector<match> out;
    for (auto const& b : blocks) {
        if (b.kind == "frontmatter") out.push_back(match_from(b));
    }
    return out;
}

contract_text contract_frontmatter(std::string_view qualifier) {
    (void)qualifier;
    return make_contract("Begin the response with a YAML frontmatter block delimited by `---` lines, "
                         "then the answer.\n",
                         "a leading YAML frontmatter block");
}

} // namespace

bool const registered_frontmatter = register_extractor(extractor{
    .name = "frontmatter",
    .summary = "the leading --- / +++ block",
    .run = &run_frontmatter,
    .contract = &contract_frontmatter,
});

} // namespace prompt::extract
