#include "include_map.hpp"
#include "../sdk/embed.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::compress::include_map {

namespace {

using std::filesystem::path;

std::string_view trim_view(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) s.remove_suffix(1);
    return s;
}

// The target of an `#include` line (`foo/bar.hpp` for `#include "foo/bar.hpp"`
// and `#include <foo/bar.hpp>`), or "" for anything else.
std::string extract_include(std::string_view line) {
    std::size_t const pos = line.find("#include");
    if (pos == std::string_view::npos) return {};
    std::string_view rest = line.substr(pos + std::string_view("#include").size());
    rest = trim_view(rest);
    if (rest.empty()) return {};
    char const open = rest.front();
    char close = '\0';
    if (open == '<') {
        close = '>';
    } else if (open == '"') {
        close = '"';
    }
    if (close == '\0') return {};
    std::size_t const end = rest.find(close, 1);
    if (end == std::string_view::npos) return {};
    return std::string(trim_view(rest.substr(1, end - 1)));
}

bool under_root(path const& root, path const& candidate) {
    std::string const r = root.generic_string();
    std::string const c = candidate.generic_string();
    if (r.empty() || c.size() <= r.size()) return false;
    if (!c.starts_with(r)) return false;
    return c[r.size()] == '/';
}

// Resolve an include target against the usual project-relative roots, keeping
// only a regular file that lives under the project root.
std::optional<path> resolve_header(path const& root, path const& file_dir, std::string const& target) {
    std::vector<path> const bases{file_dir, root, root / "include", root / "src", root / "inc"};
    for (auto const& base : bases) {
        std::error_code ec;
        path candidate = std::filesystem::weakly_canonical(base / target, ec);
        if (ec) continue;
        std::error_code type_ec;
        if (!std::filesystem::is_regular_file(candidate, type_ec) || type_ec) continue;
        if (!under_root(root, candidate)) continue;
        return candidate;
    }
    return std::nullopt;
}

std::string relative_label(path const& root, path const& p) {
    std::error_code ec;
    path const rel = std::filesystem::relative(p, root, ec);
    if (ec || rel.empty()) return p.filename().string();
    return rel.generic_string();
}

std::string decl_label(cpp::symbol const& sym) {
    if (sym.kind == "namespace") return "namespace " + sym.name;
    if (sym.kind == "class" || sym.kind == "struct" || sym.kind == "union" || sym.kind == "enum") {
        return sym.kind + " " + sym.name;
    }
    if (sym.kind == "extern") return "extern";
    if (sym.kind == "using" || sym.kind == "typedef") {
        std::string const sig = normalize_whitespace(sym.signature);
        return sig.size() <= 70 ? sig : sym.kind + " " + sym.name;
    }
    if (sym.kind == "function") {
        std::string const sig = normalize_whitespace(sym.signature);
        return sig.size() <= 70 ? sig : "function " + sym.name;
    }
    std::string const sig = normalize_whitespace(sym.signature);
    return sig.size() <= 50 ? sig : "variable " + sym.name;
}

} // namespace

std::string summarize(cpp::scan_result const& scanned, std::size_t limit) {
    std::string out;
    std::vector<std::string> seen;
    std::size_t shown = 0;
    for (auto const& sym : scanned.symbols) {
        if (sym.close || sym.kind == "access") continue;
        std::string const label = decl_label(sym);
        if (label.empty()) continue;
        if (std::ranges::find(seen, label) != seen.end()) continue;
        seen.push_back(label);
        if (shown == limit) {
            out += ", …";
            break;
        }
        if (shown != 0) out += "; ";
        out += label;
        ++shown;
    }
    return out;
}

std::string render(region const& r, signals const& s, std::span<std::string const> includes, std::size_t max_headers) {
    if (includes.empty() || max_headers == 0) return {};

    std::error_code ec;
    path root = s.project.root;
    if (root.empty()) root = std::filesystem::current_path(ec);
    if (ec) return {};
    root = std::filesystem::weakly_canonical(root, ec);
    if (ec) return {};

    path file_dir = root;
    if (!r.path.empty()) {
        path const p(r.path);
        file_dir = p.is_absolute() ? p.parent_path() : root / p.parent_path();
    }

    std::string out;
    std::vector<std::string> seen;
    std::size_t resolved = 0;
    for (auto const& raw : includes) {
        if (resolved >= max_headers) break;
        std::string const target = extract_include(raw);
        if (target.empty()) continue;
        auto const header = resolve_header(root, file_dir, target);
        if (!header) continue;
        std::string const label = relative_label(root, *header);
        if (std::ranges::find(seen, label) != seen.end()) continue;
        seen.push_back(label);

        std::error_code size_ec;
        auto const size = std::filesystem::file_size(*header, size_ec);
        if (size_ec || size > (1U << 19U)) continue;

        std::string const content = read_file(*header);
        if (content.empty()) continue;
        std::string const summary = summarize(cpp::scan(content));
        if (summary.empty()) continue;
        out += "// ";
        out += label;
        out += ": ";
        out += summary;
        out.push_back('\n');
        ++resolved;
    }
    if (out.empty()) return {};
    return "// include map (" + plural(resolved, "project header", "project headers") + "):\n" + out;
}

std::string render(region const& r, signals const& s) { return render(r, s, cpp::scan(r.text).includes); }

namespace {

bool applies_include_map(region const& r, [[maybe_unused]] signals const& s) {
    if (!r.text.contains("#include")) return false;
    bool const cpp =
        is_cpp_lang(r.lang) || (r.lang.empty() && (project::language_for_path(r.path) == project::language::cpp ||
                                                   project::language_for_path(r.path) == project::language::c));
    return cpp && !r.path.empty();
}

outcome run_include_map(region const& r, signals const& s) {
    cpp::scan_result const scanned = cpp::scan(r.text);

    std::string out;
    std::size_t const cap = 20;
    for (std::size_t i = 0; i < scanned.includes.size() && i < cap; ++i) {
        out += scanned.includes[i];
        out.push_back('\n');
    }
    if (scanned.includes.size() > cap) {
        out += "// … " + std::to_string(scanned.includes.size() - cap) + " more includes\n";
    }
    if (!out.empty()) out.push_back('\n');

    std::string const own = summarize(scanned);
    if (!own.empty()) out += "// this file: " + own + "\n";

    std::string const map = render(r, s, scanned.includes);
    if (!map.empty()) {
        out.push_back('\n');
        out += map;
    }

    outcome o;
    o.total = count_lines(r.text);
    o.markdown = std::move(out);
    o.kept = count_lines(o.markdown);
    if (o.markdown.empty() || o.kept >= o.total) return {};
    o.legend = "include.map: " + plural(scanned.includes.size(), "include", "includes") + ", " +
               plural(scanned.symbols.size(), "declaration", "declarations") + " indexed" + expand_hint(r.path);
    o.changed = true;
    return o;
}

} // namespace

bool const registered_include_map = register_compressor(compressor{
    .name = "include.map",
    .summary = "Replace a C/C++ file with its includes and a symbol index of the project headers they name.",
    .applies = &applies_include_map,
    .compress = &run_include_map,
    .priority = 5,
});

} // namespace prompt::compress::include_map
