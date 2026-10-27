#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::signals {

// What a chain is doing. Published as the `keys::mode` fact by `.fix`/`.debug`/
// `.run` and overridable with `.compress --for`; a compressor reads it to pick
// between, say, "keep the failed test bodies" and "keep a representative
// sample".
enum class intent : std::uint8_t { general, fix, debug, review };

[[nodiscard]] intent intent_from(std::string_view name);
[[nodiscard]] std::string_view to_string(intent value);

// The failed Google Test names in captured output (`[  FAILED  ] Suite.Test`),
// deduplicated, in first-seen order. This is `.run --gtest`'s and `.compress`'s
// shared parser.
[[nodiscard]] std::vector<std::string> failed_tests(std::string_view output);

// Everything the chain should treat as focal: the failed tests above plus source
// error sites (`path:line[:col]: error`, and Python `File "…", line N`) as
// `path:line`. This becomes the `keys::focus` fact.
[[nodiscard]] std::vector<std::string> extract_focus(std::string_view output);

// A cheap substring gate (no regex) for "this looks like a failing build/test
// run", so `.compress` can pick a mode from bare context.
[[nodiscard]] bool looks_like_failure(std::string_view text);

// `keys::focus` is newline-separated; these are the two ends of that encoding.
[[nodiscard]] std::vector<std::string> parse_focus(std::string_view value);
[[nodiscard]] std::string join_focus(std::span<std::string const> items);

} // namespace prompt::signals
