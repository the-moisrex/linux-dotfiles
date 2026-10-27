#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::compress::cpp {

// One declaration the scanner recognized. `signature` is the declaration header
// (everything before its `{` or `;`), normalized onto single lines.
struct symbol {
    std::string kind; // namespace|class|struct|union|enum|function|variable|using|typedef|concept
    std::string name; // best-effort identifier, for the symbol index
    std::string signature;
    std::string doc;        // attached comment immediately above, or ""
    std::size_t line = 0;   // 1-based line of the declaration
    std::size_t depth = 0;  // container nesting (for indentation)
    bool container = false; // opens a brace
    bool close = false;     // synthetic `}` closing a container
    bool has_body = false;  // a function-like body was elided
    // The signature already carries its complete body (`enum { … }`, `= { … }`
    // is skipped earlier): render it without appending `{ … }`, `;` or `:`.
    bool verbatim = false;
};

struct scan_result {
    std::vector<std::string> includes; // raw `#include …` lines
    std::vector<std::string> macros;   // raw `#define …` lines
    std::vector<symbol> symbols;       // in source order, containers included
};

// Scan a C/C++ translation unit or header. Comments and string literals are
// ignored structurally (they never open or close a brace) but a comment
// immediately above a declaration is attached as its doc. Heuristic by design:
// it handles the common declaration shapes, not every preprocessor trick.
[[nodiscard]] scan_result scan(std::string_view text);

// Render a compressed view: the includes, then each declaration's doc comment
// and signature with bodies elided and containers indented. `level` 1 keeps doc
// comments, 2 drops them, 3 keeps only the signatures (and caps the includes).
[[nodiscard]] std::string render(scan_result const& result, int level);

// The structural view of `text`: the same length, with comments and string/char
// literals blanked to spaces (newlines preserved), so braces and parentheses
// inside them are invisible to a brace counter. Units that need to find a
// matching brace without duplicating the comment/string state machine use this.
[[nodiscard]] std::string structural_view(std::string_view text);

} // namespace prompt::compress::cpp
