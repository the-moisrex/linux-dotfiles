#pragma once

// The extractor registry: the DSL behind `.expect`/`.extract`/`.exec`/`.each`
// and the standalone `md.extract` CLI. One file per extractor lives in
// `src/extract/` and registers itself here (like `src/compress/`), so adding an
// extractor is one file and one registration — never a dispatcher edit.
//
// A selector is a pipeline: `source[/refinement…]`, each stage
// `name[:qualifier][@N]`. A source stage parses the document; a refinement
// stage parses the *source* body of the previous stage, so
// `code:cpp/function:main` takes the cpp block and then the `main` function out
// of it. The grammar, the plan, the contracts and shell completion all live
// here; the document parser and the individual extractors live in
// `src/extract/`.

#include "completion.hpp"
#include "errors.hpp"
#include <cstddef>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::extract {

// One row of a table (or any record with named cells).
using cells = std::vector<std::pair<std::string, std::string>>;

// One extracted value. `text` is what a consumer (`.extract`, the next chain
// segment) sees; `source` is the raw body a following `/` stage parses. They
// differ for a section (its heading is in `text`, not in `source`) and a
// projected table (TSV in `text`, the Markdown table in `source`).
struct match {
    std::string text;
    std::string source;
    std::string kind;
    std::string lang;
    std::string lang_raw;
    std::string info;
    std::string fence;
    std::string title;
    std::string id;
    std::string file;
    std::string program;
    std::string before;
    std::string after;
    std::size_t line = 0;
    std::size_t end_line = 0;
    std::size_t index = 0;
    int heading_level = 0;
    bool closed = true;
    std::vector<std::pair<int, std::string>> heading_path;
    std::vector<cells> rows; // table rows, for `.each`
};

// One stage of a selector: `name[:qualifier][@position]`.
struct step {
    std::string name;
    std::string qualifier;
    int position = 0; // 0 = unset; 1-based, negative counts from the end
};

// One plan entry: `[bind=]step(/step)*`. An empty `bind` is the primary (its
// result replaces the chain context); a non-empty one is published as that fact.
struct selection {
    std::string bind;
    std::vector<step> steps;
};

// `run` maps an input text (the document, or a previous stage's `source`) to
// its matches. Positional `@N` is applied by the runner, after the stage.
using run_fn = std::vector<match> (*)(std::string_view text, std::string_view qualifier);

// The full instruction for a selector, and the noun phrase a combined contract
// lists it as ("one fenced code block…"). Both empty when the extractor cannot
// be the first stage of a contract.
struct contract_text {
    std::string full;
    std::string clause;
};
using contract_fn = contract_text (*)(std::string_view qualifier);
using complete_fn = void (*)(std::string_view prefix, std::ostream& os);

struct extractor {
    std::string_view name;
    std::string_view summary; // one line for help and completion
    run_fn run;
    contract_fn contract = nullptr;
    complete_fn complete = nullptr;
};

inline constexpr std::size_t extractor_capacity = 128;

bool register_extractor(extractor const& desc) noexcept;
[[nodiscard]] std::span<extractor const> get_all_extractors();
[[nodiscard]] extractor const* find_extractor(std::string_view name);

// True for a registered name or the `auto`/`any`/`all`/`none` keywords.
[[nodiscard]] bool is_extractor_name(std::string_view name);

// `[bind=]step(/step)*`. A bare word that is not an extractor name is the
// heading of the section to take (`section:<word>`). Throws usage_error on an
// empty selector or a malformed `@N`.
[[nodiscard]] selection parse_selection(std::string_view raw);

// A whole `.expect`/`.delegate` plan: newline-separated `[bind=]selector`
// entries (empty lines ignored). A malformed entry throws usage_error.
[[nodiscard]] std::vector<selection> parse_plan(std::string_view plan);
[[nodiscard]] std::string format_plan(std::span<selection const> entries);

// The unnamed primary entry, for `.each`/`.exec`. A plan with no unnamed entry
// uses its only entry; otherwise this throws usage_error.
[[nodiscard]] std::optional<selection> primary(std::string_view plan);

// `.exec`'s selector/command disambiguation: a bare extractor name, `name:…`, a
// `/` pipeline whose stages are names, or any of those with an `@N` suffix.
[[nodiscard]] bool is_selector_token(std::string_view token);

// Run a selection over one document. Throws soft_error when an `@N` is out of
// range; a stage that matches nothing simply yields no matches.
[[nodiscard]] std::vector<match> run(std::string_view document, selection const& sel);

// The contract for a selection (its first stage) and the combined instruction
// for a plan. `kinds_help` is the `--kinds` listing.
[[nodiscard]] std::string contract_for(selection const& sel);
[[nodiscard]] std::string combined_contract(std::span<selection const> entries);
[[nodiscard]] std::string kinds_help();

// The kind names a selection reads, comma-joined and sorted — what the
// "no matching … blocks" note names.
[[nodiscard]] std::string selector_kind_names(selection const& sel);

// Shell completion for a selector token: names, `name:` qualifiers, `/`
// refinements, `@N`, and the known bindings. Registered by `.expect`,
// `.extract`, `.exec` and `.each`.
void complete_selector(completion_request const& request, std::ostream& os);

} // namespace prompt::extract
