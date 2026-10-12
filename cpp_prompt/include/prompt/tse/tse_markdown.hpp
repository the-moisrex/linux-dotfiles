#pragma once

#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace prompt::tse {

// Renders a collected dataset (a live `tse all` fetch or the `data` object of
// a `.stock` snapshot) as the Markdown the prompt embeds. Byte-for-byte port
// of `bin/tse`'s `markdown()` — same headings, JSON compact dumps and CSV
// tables — so a snapshot renders exactly like a live fetch.
std::string render_markdown(nlohmann::json const& data);

} // namespace prompt::tse
