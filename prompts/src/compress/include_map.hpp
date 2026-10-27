#pragma once

#include "compress.hpp"
#include "cpp_scan.hpp"
#include <cstddef>
#include <span>
#include <string>

namespace prompt::compress::include_map {

// A compact one-line declaration list for a scanned file: `class Foo; void
// bar(int); enum Color`. Empty when nothing declaration-like was found.
[[nodiscard]] std::string summarize(cpp::scan_result const& scanned, std::size_t limit = 12);

// One line per project-local header the region includes, naming the
// declarations it provides: `// src/foo.hpp: class Foo; void bar(int)`. A header
// is only considered when it resolves inside the project root, so system and
// third-party headers stay out of the map. Empty when nothing was resolved.
[[nodiscard]] std::string render(region const& r, signals const& s, std::span<std::string const> includes,
                                 std::size_t max_headers = 8);

// Convenience: scan `r.text` for its `#include` lines first.
[[nodiscard]] std::string render(region const& r, signals const& s);

} // namespace prompt::compress::include_map
