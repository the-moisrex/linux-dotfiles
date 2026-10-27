#pragma once

// Turning blocks and matches into the text the chain (and the CLI) prints:
// per-block rendering, the `--list` inventory, and the JSON records `--json`
// and `--items` emit. `.each` reads `items_to_json`, so the item shape is the
// one `bin/md.extract --items` always produced.

#include "../sdk/extract.hpp"
#include "document.hpp"
#include <nlohmann/json.hpp>
#include <span>
#include <string>

namespace prompt::extract {

struct render_options {
    bool with_heading = false;
    bool with_intro = false;
    bool fence = false;
};

// A block as a match: `text`/`source` set per kind (a section keeps its heading
// in `text`, a command is its command line, a field is its value).
[[nodiscard]] match match_from(block const& b);

[[nodiscard]] std::string render_block(match const& m, render_options const& options);
[[nodiscard]] std::string render_many(std::span<match const> matches, render_options const& options);

// The selection output — what `.extract` and `md.extract --select` print. The
// first stage shapes it: a projected table is TSV (its header dropped with
// `no_header`), a section keeps its heading, a command is its command line; any
// other kind goes through `render_many`. Shared so the prompt and the CLI can
// never disagree.
[[nodiscard]] std::string render_selection(std::span<match const> matches, selection const& sel, bool no_header,
                                           render_options const& options);

// The selection output: each match's own `text`, folded with one blank line
// between blocks and a trailing newline (the rule `.extract` uses).
[[nodiscard]] std::string join_texts(std::span<match const> matches);

[[nodiscard]] std::string render_list(std::span<match const> matches, bool guess);
[[nodiscard]] nlohmann::ordered_json block_record(match const& m, bool guess);
[[nodiscard]] nlohmann::ordered_json items_to_json(std::span<match const> matches, bool guess);

// Choose one match interactively with fzf (the `--pick` path). Throws
// usage_error when fzf is not on PATH and soft_error when the user cancels.
[[nodiscard]] std::vector<match> pick(std::vector<match> const& matches, bool guess);

} // namespace prompt::extract
