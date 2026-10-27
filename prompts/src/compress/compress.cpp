#include "compress.hpp"
#include "../core/fs.hpp"
#include "../core/strings.hpp"
#include "../sdk/embed.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace prompt::compress {

namespace {

std::array<compressor, compressor_capacity> g_units{};
std::size_t g_count = 0;

std::string_view trim_view(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string normalize_lang(std::string_view info) {
    // The info string may carry extra attributes ("cpp title=…"): keep the first
    // word and lower-case it, mapping the C++ spellings to "cpp".
    std::size_t end = 0;
    while (end < info.size() && std::isspace(static_cast<unsigned char>(info[end])) == 0 && info[end] != ',') ++end;
    std::string out;
    out.reserve(end);
    for (std::size_t i = 0; i < end; ++i) {
        char const c = info[i];
        out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }
    if (out == "c++" || out == "cxx" || out == "cc") out = "cpp";
    if (out == "cplusplus") out = "cpp";
    if (out == "h" || out == "hpp" || out == "hxx") out = "cpp";
    return out;
}

struct fence {
    char ch = '`';
    std::size_t len = 0;
    std::string info;
};

// A CommonMark opening fence: up to three leading spaces, then >=3 backticks or
// tildes, then an optional info string.
std::optional<fence> parse_opening(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ' && i < 3) ++i;
    if (i >= line.size()) return std::nullopt;
    char const c = line[i];
    if (c != '`' && c != '~') return std::nullopt;
    std::size_t j = i;
    while (j < line.size() && line[j] == c) ++j;
    if (j - i < 3) return std::nullopt;
    return fence{.ch = c, .len = j - i, .info = std::string(trim_view(line.substr(j)))};
}

bool is_closing(std::string_view line, char c, std::size_t len) {
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    std::size_t j = i;
    while (j < line.size() && line[j] == c) ++j;
    if (j - i < len) return false;
    while (j < line.size() && line[j] == ' ') ++j;
    return j == line.size();
}

std::string_view strip_location(std::string_view entry) {
    std::size_t colon = entry.rfind(':');
    while (colon != std::string_view::npos) {
        std::string_view const tail = entry.substr(colon + 1);
        bool const numeric = !tail.empty() && std::ranges::all_of(tail, [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
        if (!numeric) break;
        entry = entry.substr(0, colon);
        colon = entry.rfind(':');
    }
    return entry;
}

bool looks_like_path(std::string_view s) {
    if (s.empty() || s.contains(' ') || s.contains('\t')) return false;
    std::size_t const dot = s.rfind('.');
    if (dot == std::string_view::npos) return s.contains('/');
    std::string_view const ext = s.substr(dot + 1);
    return !ext.empty() && std::ranges::all_of(ext, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '+' || c == '#';
    });
}

// The path a fence heading names, or nullopt. Handles "File src/a.cpp",
// "### src/a.cpp", "`src/a.cpp`" and a bare "src/a.cpp".
std::optional<std::string> path_from_line(std::string_view line) {
    std::string_view rest = trim_view(line);
    if (rest.empty()) return std::nullopt;
    std::size_t i = 0;
    while (i < rest.size() && rest[i] == '#') ++i;
    while (i < rest.size() && rest[i] == ' ') ++i;
    rest = rest.substr(i);
    for (std::string_view const prefix : {"File:", "File", "file:", "file"}) {
        if (rest.starts_with(prefix) && (rest.size() == prefix.size() || rest[prefix.size()] == ' ')) {
            rest = trim_view(rest.substr(prefix.size()));
            break;
        }
    }
    if (rest.size() >= 2 && rest.front() == '`' && rest.back() == '`') rest = rest.substr(1, rest.size() - 2);
    if (rest.size() >= 4 && rest.starts_with("**") && rest.ends_with("**")) rest = rest.substr(2, rest.size() - 4);
    rest = trim_view(rest);
    if (!looks_like_path(rest)) return std::nullopt;
    return std::string(rest);
}

std::string_view basename_of(std::string_view path) {
    std::size_t const slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool same_file(std::string_view a, std::string_view b) {
    while (a.starts_with("./")) a.remove_prefix(2);
    while (b.starts_with("./")) b.remove_prefix(2);
    if (a == b) return true;
    if (a.size() > b.size() && a.ends_with(b) && a[a.size() - b.size() - 1] == '/') return true;
    if (b.size() > a.size() && b.ends_with(a) && b[b.size() - a.size() - 1] == '/') return true;
    return basename_of(a) == basename_of(b);
}

bool is_focused(region const& r, std::vector<std::string> const& focus) {
    if (r.path.empty()) return false;
    return std::ranges::any_of(focus, [&](std::string const& entry) {
        std::string_view const path = strip_location(entry);
        return !path.empty() && looks_like_path(path) && same_file(r.path, path);
    });
}

compressor const* select(region const& r, signals const& s, options const& opt) {
    compressor const* best = nullptr;
    for (auto const& unit : get_all_compressors()) {
        if (!allowed(unit, opt) || !unit.applies(r, s)) continue;
        if (best == nullptr || unit.priority > best->priority) best = &unit;
    }
    return best;
}

struct rendered {
    std::string text;
    std::size_t after = 0;
    std::size_t regions = 0;
    bool changed = false;
};

} // namespace

bool register_compressor(compressor const& desc) noexcept {
    if (g_count >= g_units.size()) {
        std::fputs("prompt: compressor registry is full; raise compressor_capacity\n", stderr);
        std::abort();
    }
    g_units[g_count++] = desc;
    return true;
}

std::span<compressor const> get_all_compressors() { return {g_units.data(), g_count}; }

compressor const* find_compressor(std::string_view name) {
    for (std::size_t i = 0; i < g_count; ++i) {
        if (g_units[i].name == name) return &g_units[i];
    }
    return nullptr;
}

bool allowed(compressor const& desc, options const& opt) {
    if (!opt.only.empty() && std::ranges::find(opt.only, desc.name) == opt.only.end()) return false;
    return std::ranges::find(opt.skip, desc.name) == opt.skip.end();
}

std::string fence_for(std::string_view lang) { return lang.empty() ? "```" : "```" + std::string(lang); }

bool is_cpp_lang(std::string_view lang) {
    static constexpr std::array<std::string_view, 12> langs = {
        "c", "cpp", "c++", "cc", "cxx", "h", "hpp", "hxx", "hh", "ipp", "objective-c", "objective-c++",
    };
    return std::ranges::find(langs, lang) != langs.end();
}

std::size_t count_lines(std::string_view text) {
    if (text.empty()) return 0;
    std::size_t lines = static_cast<std::size_t>(std::ranges::count(text, '\n'));
    if (text.back() != '\n') ++lines;
    return lines;
}

std::string plural(std::size_t n, std::string_view singular, std::string_view many) {
    return std::to_string(n) + " " + std::string(n == 1 ? singular : many);
}

std::string first_items(std::span<std::string const> items, std::size_t limit) {
    std::string out;
    std::size_t shown = 0;
    for (auto const& item : items) {
        if (item.empty()) continue;
        if (shown == limit) {
            out += ", …";
            break;
        }
        if (shown != 0) out += ", ";
        out += item;
        ++shown;
    }
    return out;
}

std::string normalize_whitespace(std::string_view text) {
    std::string out;
    bool pending_space = false;
    for (char const c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) out.push_back(' ');
        pending_space = false;
        out.push_back(c);
    }
    return out;
}

std::string expand_hint(std::string_view path, std::string_view extra) {
    std::string out;
    if (!extra.empty()) {
        out = " Expand: " + std::string(extra);
    }
    if (!path.empty()) {
        if (!out.empty()) out += " or `prompt .files " + std::string(path) + "`.";
        else
            out = " Expand: `prompt .files " + std::string(path) + "`.";
    }
    return out;
}

std::vector<piece> scan_markdown(std::string_view markdown) {
    std::vector<piece> pieces;
    std::string literal;
    std::size_t pos = 0;
    std::size_t line_no = 1;

    auto flush_literal = [&] {
        if (literal.empty()) return;
        piece p;
        p.raw = std::move(literal);
        literal.clear();
        pieces.push_back(std::move(p));
    };

    while (pos < markdown.size()) {
        std::size_t const nl = markdown.find('\n', pos);
        std::size_t const line_end = nl == std::string_view::npos ? markdown.size() : nl;
        std::string_view const line = markdown.substr(pos, line_end - pos);

        auto const opening = parse_opening(line);
        if (opening) {
            std::size_t const body_start = nl == std::string_view::npos ? markdown.size() : nl + 1;
            std::size_t cursor = body_start;
            std::size_t close_start = std::string_view::npos;
            std::size_t close_end = std::string_view::npos;
            while (cursor < markdown.size()) {
                std::size_t const nl2 = markdown.find('\n', cursor);
                std::size_t const le = nl2 == std::string_view::npos ? markdown.size() : nl2;
                if (is_closing(markdown.substr(cursor, le - cursor), opening->ch, opening->len)) {
                    close_start = cursor;
                    close_end = le;
                    break;
                }
                if (nl2 == std::string_view::npos) break;
                cursor = nl2 + 1;
            }

            if (close_start != std::string_view::npos) {
                flush_literal();
                std::size_t const raw_end = close_end < markdown.size() ? close_end + 1 : close_end;
                std::string raw(markdown.substr(pos, raw_end - pos));

                piece p;
                p.fenced = true;
                p.raw = raw;
                p.opening = std::string(line);
                p.marker = std::string(opening->len, opening->ch);
                p.body.lang = normalize_lang(opening->info);
                p.body.line = line_no + 1;
                std::string body(markdown.substr(body_start, close_start > body_start ? close_start - body_start : 0));
                if (!body.empty() && body.back() == '\n') body.pop_back();
                p.body.text = std::move(body);
                pieces.push_back(std::move(p));

                line_no += static_cast<std::size_t>(std::ranges::count(raw, '\n'));
                pos = raw_end;
                continue;
            }
        }

        literal.append(line);
        if (nl != std::string_view::npos) literal.push_back('\n');
        pos = nl == std::string_view::npos ? markdown.size() : nl + 1;
        ++line_no;
    }
    flush_literal();

    // Attach each fenced region's path from the nearest preceding non-empty
    // literal line ("File src/a.cpp", "### src/a.cpp", a bare path).
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (!pieces[i].fenced) continue;
        // Only the immediately preceding literal chunk can name this fence.
        if (i == 0 || pieces[i - 1].fenced) continue;
        auto const lines = text::split_lines(pieces[i - 1].raw, true);
        for (auto const& line : std::views::reverse(lines)) {
            if (line.empty()) continue;
            if (auto const path = path_from_line(line)) pieces[i].body.path = *path;
            break;
        }
    }
    return pieces;
}

report compress_document(std::string_view markdown, signals base, options const& opt) {
    std::vector<piece> pieces = scan_markdown(markdown);
    for (auto& p : pieces) {
        if (p.fenced) p.body.focused = is_focused(p.body, base.focus);
    }

    report rep;
    rep.before = count_lines(markdown);

    auto render = [&](int level) {
        signals s = base;
        s.level = level;
        rendered out;
        for (auto const& p : pieces) {
            if (!p.fenced) {
                out.text += p.raw;
                out.after += count_lines(p.raw);
                continue;
            }
            std::size_t const lines = count_lines(p.body.text);
            compressor const* unit = nullptr;
            if (!p.body.focused && (opt.force || lines >= opt.min_lines)) unit = select(p.body, s, opt);
            if (unit == nullptr) {
                out.text += p.raw;
                out.after += count_lines(p.raw);
                continue;
            }
            outcome const o = unit->compress(p.body, s);
            if (!o.changed) {
                out.text += p.raw;
                out.after += count_lines(p.raw);
                continue;
            }
            out.text += p.opening;
            out.text.push_back('\n');
            out.text += o.markdown;
            if (o.markdown.empty() || o.markdown.back() != '\n') out.text.push_back('\n');
            out.text += p.marker;
            out.text.push_back('\n');
            out.after += count_lines(p.opening) + 1 + count_lines(o.markdown) + 1;
            if (!o.legend.empty()) {
                out.text += o.legend;
                out.text.push_back('\n');
                out.after += count_lines(o.legend) + 1;
            }
            ++out.regions;
            out.changed = true;
        }
        return out;
    };

    int level = opt.level > 0 ? opt.level : 1;
    rendered result = render(level);
    if (opt.budget_lines) {
        for (int guard = 0; result.after > *opt.budget_lines && level < 3 && guard < 4; ++guard) {
            result = render(++level);
        }
    }

    rep.after = result.after;
    rep.regions = result.regions;
    rep.changed = result.changed;
    if (result.changed) {
        rep.text = std::format("[compressed] {} · {} → {} lines.\n\n", plural(result.regions, "region", "regions"),
                               rep.before, rep.after) +
                   result.text;
    } else {
        rep.text = std::string(markdown);
    }
    return rep;
}

report compress_files(std::span<std::filesystem::path const> files, signals base, options const& opt) {
    std::string document;
    for (auto const& file : files) {
        std::string const content = read_file(file);
        if (content.empty()) continue;
        auto const rel = fs::relative_path(
            file, base.project.root.empty() ? std::nullopt : std::optional<std::filesystem::path>(base.project.root));
        document += std::format("### {}\n\n{}\n", rel.string(), fence_for(infer_lang(file)));
        document += content;
        if (content.back() != '\n') document.push_back('\n');
        document += "```\n\n";
    }
    return compress_document(document, std::move(base), opt);
}

std::vector<std::string> explain(std::string_view markdown, signals const& base, options const& opt) {
    std::vector<piece> pieces = scan_markdown(markdown);
    std::vector<std::string> lines;
    for (auto& p : pieces) {
        if (!p.fenced) continue;
        p.body.focused = is_focused(p.body, base.focus);
        std::string const label = p.body.path.empty() ? std::string("(block)") : p.body.path;
        std::string const lang = p.body.lang.empty() ? std::string("?") : p.body.lang;
        std::size_t const count = count_lines(p.body.text);
        if (p.body.focused) {
            lines.push_back(std::format("{} · {} · keep (focal)", label, lang));
            continue;
        }
        if (!opt.force && count < opt.min_lines) {
            lines.push_back(std::format("{} · {} · keep ({} < {})", label, lang, count, opt.min_lines));
            continue;
        }
        signals s = base;
        s.level = opt.level > 0 ? opt.level : 1;
        compressor const* unit = select(p.body, s, opt);
        if (unit == nullptr) {
            lines.push_back(std::format("{} · {} · keep (no compressor)", label, lang));
            continue;
        }
        lines.push_back(std::format("{} · {} · {} lines → {}", label, lang, count, unit->name));
    }
    return lines;
}

} // namespace prompt::compress
