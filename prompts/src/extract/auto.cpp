#include "../sdk/errors.hpp"
#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <array>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

// A selected section subsumes the blocks under it, so `any` keeps only the
// top-level ones.
std::vector<block> drop_nested(std::vector<block> const& blocks) {
    std::vector<std::size_t> sections;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].kind == "section") sections.push_back(i);
    }
    std::vector<block> out;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        bool nested = false;
        for (auto const j : sections) {
            if (j == i) continue;
            auto const& section = blocks[j];
            if (section.file == blocks[i].file && section.start_line <= blocks[i].start_line &&
                blocks[i].start_line <= section.end_line) {
                nested = true;
                break;
            }
        }
        if (!nested) out.push_back(blocks[i]);
    }
    return out;
}

std::vector<match> run_any(std::string_view text, std::string_view qualifier) {
    (void)qualifier;
    auto blocks = parse_document(text, "");
    std::vector<block> source;
    for (auto const& b : blocks) {
        if (b.kind == "code" || b.kind == "table" || b.kind == "section" || b.kind == "frontmatter")
            source.push_back(b);
    }
    auto top = drop_nested(source);
    assign_indices(top);
    std::vector<match> out;
    out.reserve(top.size());
    for (auto const& b : top) out.push_back(match_from(b));
    return out;
}

std::vector<match> run_auto(std::string_view text, std::string_view qualifier) {
    (void)qualifier;
    auto blocks = parse_document(text, "");
    assign_indices(blocks);
    std::vector<block> tables;
    std::vector<block> code;
    std::vector<block> sections;
    for (auto const& b : blocks) {
        if (b.kind == "table") tables.push_back(b);
        else if (b.kind == "code")
            code.push_back(b);
        else if (b.kind == "section")
            sections.push_back(b);
    }
    std::array<std::vector<block> const*, 3> const groups{&tables, &code, &sections};
    std::vector<block> const* only = nullptr;
    int present = 0;
    for (auto const* group : groups) {
        if (group->empty()) continue;
        ++present;
        only = group;
    }
    if (present == 1 && only->size() == 1) return {match_from(only->front())};
    std::string counts;
    if (!tables.empty()) counts += std::format("{} table", tables.size());
    if (!code.empty()) {
        if (!counts.empty()) counts += ", ";
        counts += std::format("{} code", code.size());
    }
    if (!sections.empty()) {
        if (!counts.empty()) counts += ", ";
        counts += std::format("{} section", sections.size());
    }
    if (counts.empty()) counts = "no blocks";
    throw soft_error{std::format(
        "cannot auto-select: {}; pass a selector (e.g. table, table:column, code:sh, command:prompt)\n", counts)};
}

std::vector<match> run_none(std::string_view text, std::string_view qualifier) {
    (void)text;
    (void)qualifier;
    throw soft_error{"the 'none' selector has no blocks\n"};
}

contract_text contract_any(std::string_view qualifier) {
    (void)qualifier;
    return make_contract("Return only the requested block or blocks, with no surrounding commentary.\n",
                         "the requested result");
}

contract_text contract_none(std::string_view qualifier) {
    (void)qualifier;
    return make_contract("Answer in plain prose. Do not add code fences, tables or headings unless the answer "
                         "genuinely needs them.\n",
                         "plain prose");
}

} // namespace

bool const registered_any = register_extractor(extractor{
    .name = "any",
    .summary = "the four source kinds (code, table, section, frontmatter)",
    .run = &run_any,
    .contract = &contract_any,
});

bool const registered_auto = register_extractor(extractor{
    .name = "auto",
    .summary = "the only table, code block or section in the document",
    .run = &run_auto,
});

bool const registered_none = register_extractor(extractor{
    .name = "none",
    .summary = "select nothing (and ask for plain prose)",
    .run = &run_none,
    .contract = &contract_none,
});

} // namespace prompt::extract
