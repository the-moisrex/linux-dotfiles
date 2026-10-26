#include "completion.hpp"
#include "prompt_registry.hpp"
#include <algorithm>
#include <clocale>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace prompt {

namespace {

// Locale-aware name ordering, the collation the catalog itself uses.
bool name_less(std::string_view a, std::string_view b) {
    static bool const locale_set = std::setlocale(LC_COLLATE, "") != nullptr;
    (void)locale_set;
    std::string const ka(a);
    std::string const kb(b);
    int const c = std::strcoll(ka.c_str(), kb.c_str());
    if (c != 0) return c < 0;
    return a < b; // collation-equal names: deterministic tiebreak
}

} // namespace

void write_prompt_candidates(prompt_descriptor const& desc, std::string_view prefix,
                             std::span<std::string_view const> prior_args, std::ostream& out) {
    // Flags first (they are what an empty prefix mostly wants), then whatever
    // the prompt knows about its own operands.
    if (desc.render_help_fn != nullptr) {
        std::ostringstream help;
        desc.render_help_fn(help);
        completion::write_help_flags(out, prefix, help.str());
    }
    if (desc.complete_fn != nullptr) {
        desc.complete_fn(completion_request{.prefix = prefix, .prior_args = prior_args}, out);
    }
}

void write_step_names(std::ostream& out, std::string_view prefix, std::span<name_candidate const> candidates) {
    auto const prompts = get_all_prompts();
    std::vector<name_candidate> all;
    all.reserve(prompts.size() + candidates.size());
    for (auto const& desc : prompts) {
        all.push_back({.name = std::string(desc.name), .description = std::string(desc.help_summary)});
    }
    for (auto const& cand : candidates) all.push_back(cand);
    std::sort(all.begin(), all.end(),
              [](name_candidate const& a, name_candidate const& b) { return name_less(a.name, b.name); });

    for (auto const& cand : all) {
        if (completion::matches(prefix, cand.name)) completion::write_item(out, cand.name, cand.description);
        // `.name`/`-name` are the same step; offer them so a chain step can be
        // completed with the prefix the user is already typing.
        std::string alternate = "." + cand.name;
        if (completion::matches(prefix, alternate)) completion::write_item(out, alternate, cand.description);
        alternate = "-" + cand.name;
        if (completion::matches(prefix, alternate)) completion::write_item(out, alternate, cand.description);
    }
}

void write_prompt_names(std::ostream& out, std::string_view prefix) { write_step_names(out, prefix); }

} // namespace prompt
