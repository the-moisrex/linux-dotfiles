#include "cpp_symbols.hpp"
#include "../sdk/project.hpp"
#include "cpp_scan.hpp"
#include "include_map.hpp"
#include <string>

namespace prompt::compress::cpp_symbols {

namespace {

bool applies_cpp(region const& r, [[maybe_unused]] signals const& s) {
    if (!r.text.empty() && is_cpp_lang(r.lang)) return true;
    if (r.lang.empty() && !r.path.empty()) {
        project::language const lang = project::language_for_path(r.path);
        if (lang == project::language::cpp || lang == project::language::c) return true;
    }
    return false;
}

} // namespace

outcome compress(region const& r, signals const& s) {
    cpp::scan_result const scanned = cpp::scan(r.text);

    outcome o;
    o.total = count_lines(r.text);
    if (scanned.symbols.empty() && scanned.includes.empty() && scanned.macros.empty()) return o;

    std::string markdown = cpp::render(scanned, s.level);

    // The include map is the "where is this defined" half of the compression:
    // the declarations of this file plus a compact symbol index of the
    // project-local headers it includes.
    std::string const map = include_map::render(r, s, scanned.includes);
    if (!map.empty()) {
        markdown.push_back('\n');
        markdown += map;
    }

    o.markdown = std::move(markdown);
    o.kept = count_lines(o.markdown);
    if (o.kept >= o.total) return {};

    std::size_t declarations = 0;
    bool bodies = false;
    for (auto const& sym : scanned.symbols) {
        if (sym.close || sym.kind == "access") continue;
        ++declarations;
        bodies = bodies || sym.has_body;
    }

    std::string legend = "cpp.symbols: " + plural(declarations, "declaration", "declarations") + ", " +
                         plural(scanned.includes.size(), "include", "includes");
    if (s.level >= 2) legend += ", doc comments dropped";
    if (s.level >= 3) legend += ", macros dropped";
    if (bodies) legend += ", function bodies elided";
    if (!map.empty()) legend += ", include map appended";
    legend += expand_hint(r.path);

    o.legend = std::move(legend);
    o.changed = true;
    return o;
}

bool const registered = register_compressor(compressor{
    .name = "cpp.symbols",
    .summary = "Replace a C/C++ file with its declarations, doc comments and an include map.",
    .applies = &applies_cpp,
    .compress = &compress,
    .priority = 10,
});

} // namespace prompt::compress::cpp_symbols
