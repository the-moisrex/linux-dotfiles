#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::string lowered(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char const c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::vector<match> run_command(std::string_view text, std::string_view qualifier) {
    auto blocks = parse_document(text, "");
    std::vector<block> derived;
    for (auto const& b : blocks) {
        if (b.kind != "code") continue;
        if (auto command = derive_command(b); command.has_value()) derived.push_back(std::move(*command));
    }
    assign_indices(derived);

    auto const wanted = lowered(trim_view(qualifier));
    std::vector<match> out;
    for (auto const& b : derived) {
        if (!wanted.empty() && wanted != "*" && lowered(b.program) != wanted) continue;
        out.push_back(match_from(b));
    }
    return out;
}

contract_text contract_command(std::string_view qualifier) {
    std::string const program =
        trim_view(qualifier).empty() ? std::string{} : std::format(" `{}`", trim_view(qualifier));
    return make_contract(std::format("Return exactly one fenced `sh` code block containing a single{} command line. "
                                     "Put no prose, no explanation and no other code blocks before or after it.\n",
                                     program),
                         std::format("one fenced `sh` code block containing a single{} command line", program));
}

void complete_command(std::string_view prefix, std::ostream& os) {
    for (auto const* name : {"prompt", "git", "cmake", "make", "*"}) {
        if (completion::matches(prefix, name)) completion::write_item(os, name, "program basename");
    }
}

} // namespace

bool const registered_command = register_extractor(extractor{
    .name = "command",
    .summary = "a code block viewed as the command on its first line; command:program filters by basename",
    .run = &run_command,
    .contract = &contract_command,
    .complete = &complete_command,
});

} // namespace prompt::extract
