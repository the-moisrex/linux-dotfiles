#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::optional<std::string> as_filename(std::string_view line) {
    auto text = clean_field_value(trim_view(line));
    if (text.empty() || text.size() > 120) return std::nullopt;
    if (text.front() == '#' || text.front() == '`' || text.front() == '|' || text.front() == '\\') return std::nullopt;
    // A bare prose sentence has spaces and no extension; a name may carry one
    // space, but must end in an extension or be a single token.
    auto const spaces = static_cast<std::size_t>(std::count(text.begin(), text.end(), ' '));
    if (spaces == 0) return text;
    if (spaces == 1 && text.contains('.')) return text;
    return std::nullopt;
}

std::vector<match> make(std::string name) {
    match m;
    m.kind = "filename";
    m.text = std::move(name);
    m.source = m.text;
    return {std::move(m)};
}

std::vector<match> run_filename(std::string_view text, std::string_view qualifier) {
    (void)qualifier;
    for (auto const* label : {"filename", "file", "name"}) {
        for (auto const& hit : find_fields(text, label)) return make(hit.value);
    }
    auto blocks = parse_document(text, "");
    for (auto const& b : blocks) {
        if (b.kind != "frontmatter") continue;
        for (auto const* label : {"filename", "file", "name"}) {
            for (auto const& hit : find_fields(b.body, label)) return make(hit.value);
        }
    }
    for (auto const& b : blocks) {
        if (b.kind != "code") continue;
        std::size_t lines = 0;
        for (char const c : b.body) {
            if (c == '\n') ++lines;
        }
        if (lines > 1) continue;
        if (auto name = as_filename(b.body); name.has_value()) return make(*name);
    }
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find('\n', begin);
        auto const line = text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin);
        if (auto name = as_filename(line); name.has_value()) return make(*name);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return {};
}

contract_text contract_filename(std::string_view qualifier) {
    (void)qualifier;
    return make_contract("Also name the file: put a line `filename: <name>` carrying the proposed file name, "
                         "on a line of its own.\n",
                         "a line `filename: <name>` carrying the proposed file name");
}

} // namespace

bool const registered_filename = register_extractor(extractor{
    .name = "filename",
    .summary = "the file name the answer proposes (a `filename:` line, a bare name or a one-line block)",
    .run = &run_filename,
    .contract = &contract_filename,
});

} // namespace prompt::extract
