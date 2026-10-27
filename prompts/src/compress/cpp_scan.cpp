#include "cpp_scan.hpp"
#include "../core/strings.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::compress::cpp {

namespace {

bool is_word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

std::string_view trim_view(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) s.remove_suffix(1);
    return s;
}

bool has_word(std::string_view hay, std::string_view word) {
    std::size_t pos = 0;
    while ((pos = hay.find(word, pos)) != std::string_view::npos) {
        bool const left = pos == 0 || !is_word_char(hay[pos - 1]);
        bool const right = pos + word.size() >= hay.size() || !is_word_char(hay[pos + word.size()]);
        if (left && right) return true;
        ++pos;
    }
    return false;
}

// `raw` with each line trimmed, runs of spaces collapsed and blank lines
// dropped, joined by newlines. Comment blanks left by the structural pass never
// reach a signature (signatures are sliced from the original text).
std::string normalize(std::string_view raw) {
    std::string out;
    std::size_t pos = 0;
    while (pos <= raw.size()) {
        std::size_t const nl = raw.find('\n', pos);
        std::string_view line = raw.substr(pos, nl == std::string_view::npos ? raw.size() - pos : nl - pos);
        line = trim_view(line);
        if (!line.empty()) {
            if (!out.empty()) out.push_back('\n');
            bool space = false;
            for (char const c : line) {
                if (c == ' ' || c == '\t') {
                    space = true;
                    continue;
                }
                if (space && !out.empty() && out.back() != '\n') out.push_back(' ');
                space = false;
                out.push_back(c);
            }
        }
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    return out;
}

std::string identifier_after(std::string_view sig, std::string_view keyword) {
    std::size_t pos = sig.find(keyword);
    if (pos == std::string_view::npos) return {};
    pos += keyword.size();
    while (pos < sig.size() && !is_word_char(sig[pos])) ++pos;
    std::size_t const start = pos;
    while (pos < sig.size() && is_word_char(sig[pos])) ++pos;
    return std::string(sig.substr(start, pos - start));
}

std::string last_identifier(std::string_view sig) {
    std::size_t i = sig.size();
    while (i > 0) {
        while (i > 0 && !is_word_char(sig[i - 1])) --i;
        std::size_t const end = i;
        while (i > 0 && is_word_char(sig[i - 1])) --i;
        if (end > i) return std::string(sig.substr(i, end - i));
    }
    return {};
}

std::string variable_name(std::string_view sig) {
    std::size_t const cut = sig.find_first_of("=[");
    return last_identifier(sig.substr(0, cut));
}

std::string name_before_paren(std::string_view sig) {
    std::size_t const p = sig.find('(');
    return last_identifier(sig.substr(0, p == std::string_view::npos ? sig.size() : p));
}

// Remove a leading `template <…>` (balanced angle brackets) so the container
// keyword that follows is classified correctly.
std::string_view strip_template(std::string_view sig) {
    std::string_view s = trim_view(sig);
    if (!s.starts_with("template")) return s;
    std::size_t i = std::string_view("template").size();
    if (i < s.size() && is_word_char(s[i])) return s;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) ++i;
    if (i >= s.size() || s[i] != '<') return s;
    int depth = 0;
    for (; i < s.size(); ++i) {
        if (s[i] == '<') {
            ++depth;
        } else if (s[i] == '>' && --depth == 0) {
            ++i;
            break;
        }
    }
    return trim_view(s.substr(i));
}

std::string clean_line_comment(std::string_view raw) {
    std::size_t i = 0;
    while (i < raw.size() && raw[i] == '/') ++i;
    return std::string(trim_view(raw.substr(i)));
}

std::string clean_block_comment(std::string_view raw) {
    std::string_view body = raw;
    if (body.starts_with("/*")) body = body.substr(2);
    if (body.ends_with("*/")) body.remove_suffix(2);
    std::vector<std::string> lines;
    for (auto const& line : text::split_lines(body, true)) {
        std::string_view l = trim_view(line);
        if (!l.empty() && l.front() == '*') {
            l.remove_prefix(1);
            if (!l.empty() && l.front() == ' ') l.remove_prefix(1);
        }
        lines.emplace_back(l);
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
    std::string out;
    for (auto const& line : lines) {
        if (!out.empty()) out.push_back('\n');
        out += line;
    }
    return out;
}

struct comment {
    std::size_t start_line = 0;
    std::size_t end_line = 0;
    std::string text;
};

std::size_t skip_quoted(std::string_view text, std::size_t i, char quote, std::string& code) {
    ++i;
    while (i < text.size()) {
        char const c = text[i];
        if (c == '\\') {
            i += i + 1 < text.size() ? 2 : 1;
            continue;
        }
        if (c == '\n') code[i] = '\n';
        if (c == quote) {
            ++i;
            break;
        }
        ++i;
    }
    return i;
}

std::size_t skip_raw(std::string_view text, std::size_t i, std::string& code) {
    std::size_t const open = text.find('(', i + 2);
    if (open == std::string_view::npos) return i + 1;
    std::string const delimiter(text.substr(i + 2, open - (i + 2)));
    std::string const closer = ")" + delimiter + "\"";
    std::size_t const close = text.find(closer, open + 1);
    std::size_t const end = close == std::string_view::npos ? text.size() : close + closer.size();
    for (std::size_t k = i; k < end; ++k) {
        if (text[k] == '\n') code[k] = '\n';
    }
    return end;
}

// The structural view: `code` is as long as `text`, with comments and string
// literals blanked to spaces (newlines preserved), so braces/parens in them are
// invisible. Comments are collected for doc attachment.
struct stripped {
    std::string code;
    std::vector<std::size_t> line_starts;
};

stripped strip_impl(std::string_view text, std::vector<comment>* comments) {
    stripped out;
    out.code.assign(text.size(), ' ');
    out.line_starts.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            out.line_starts.push_back(i + 1);
            out.code[i] = '\n';
        }
    }
    auto line_of = [&](std::size_t index) {
        auto const it = std::upper_bound(out.line_starts.begin(), out.line_starts.end(), index);
        return static_cast<std::size_t>(it - out.line_starts.begin());
    };

    std::size_t i = 0;
    while (i < text.size()) {
        char const c = text[i];
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            std::size_t end = text.find('\n', i);
            if (end == std::string_view::npos) end = text.size();
            if (comments != nullptr) {
                comments->push_back(comment{
                    .start_line = line_of(i),
                    .end_line = line_of(end == 0 ? 0 : end - 1),
                    .text = clean_line_comment(text.substr(i, end - i)),
                });
            }
            i = end;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            std::size_t const close = text.find("*/", i + 2);
            std::size_t const end = close == std::string_view::npos ? text.size() : close + 2;
            if (comments != nullptr) {
                comments->push_back(comment{
                    .start_line = line_of(i),
                    .end_line = line_of(end - 1),
                    .text = clean_block_comment(text.substr(i, end - i)),
                });
            }
            for (std::size_t k = i; k < end; ++k) {
                if (text[k] == '\n') out.code[k] = '\n';
            }
            i = end;
            continue;
        }
        if (c == '"') {
            i = skip_quoted(text, i, '"', out.code);
            continue;
        }
        if (c == '\'') {
            i = skip_quoted(text, i, '\'', out.code);
            continue;
        }
        if (c == 'R' && i + 1 < text.size() && text[i + 1] == '"') {
            i = skip_raw(text, i, out.code);
            continue;
        }
        out.code[i] = c;
        ++i;
    }
    return out;
}

stripped strip(std::string_view text, std::vector<comment>& comments) { return strip_impl(text, &comments); }

std::size_t skip_brace(std::string_view code, std::size_t open) {
    int depth = 0;
    for (std::size_t k = open; k < code.size(); ++k) {
        if (code[k] == '{') {
            ++depth;
        } else if (code[k] == '}' && --depth == 0) {
            return k + 1;
        }
    }
    return code.size();
}

bool is_access(std::string_view head) {
    static constexpr std::array<std::string_view, 5> words = {"public", "private", "protected", "signals", "slots"};
    return std::ranges::any_of(
        words, [&](std::string_view word) { return head == word || head == "public " + std::string(word); });
}

void classify(symbol& sym, bool opening) {
    std::string_view const sig = strip_template(sym.signature);
    if (opening) {
        if (has_word(sig, "namespace")) {
            sym.kind = "namespace";
            sym.name = identifier_after(sig, "namespace");
            sym.container = true;
        } else if (has_word(sig, "class")) {
            sym.kind = "class";
            sym.name = identifier_after(sig, "class");
            sym.container = true;
        } else if (has_word(sig, "struct")) {
            sym.kind = "struct";
            sym.name = identifier_after(sig, "struct");
            sym.container = true;
        } else if (has_word(sig, "union")) {
            sym.kind = "union";
            sym.name = identifier_after(sig, "union");
            sym.container = true;
        } else if (has_word(sig, "extern")) {
            sym.kind = "extern";
            sym.container = true;
        } else if (has_word(sig, "enum")) {
            sym.kind = "enum";
            sym.name = identifier_after(sig, "enum");
            sym.verbatim = true;
        } else {
            sym.kind = "function";
            sym.name = name_before_paren(sig);
            sym.has_body = true;
        }
        return;
    }
    if (has_word(sig, "using")) {
        sym.kind = "using";
        sym.name = identifier_after(sig, "using");
    } else if (has_word(sig, "typedef")) {
        sym.kind = "typedef";
        sym.name = last_identifier(sig);
    } else if (sig.contains('(')) {
        sym.kind = "function";
        sym.name = name_before_paren(sig);
    } else if (has_word(sig, "class")) {
        sym.kind = "class";
        sym.name = identifier_after(sig, "class");
    } else if (has_word(sig, "struct")) {
        sym.kind = "struct";
        sym.name = identifier_after(sig, "struct");
    } else {
        sym.kind = "variable";
        sym.name = variable_name(sig);
    }
}

} // namespace

std::string structural_view(std::string_view text) { return strip_impl(text, nullptr).code; }

scan_result scan(std::string_view text) {
    std::vector<comment> comments;
    stripped const data = strip(text, comments);
    std::string_view const code = data.code;

    auto line_of = [&](std::size_t index) {
        auto const it = std::upper_bound(data.line_starts.begin(), data.line_starts.end(), index);
        return static_cast<std::size_t>(it - data.line_starts.begin());
    };

    scan_result result;
    std::vector<std::size_t> container_depth; // depth of each open container
    std::size_t paren = 0;
    std::size_t bracket = 0;
    bool in_stmt = false;
    std::size_t stmt_start = 0;

    auto attach_doc = [&](symbol& sym) {
        std::size_t want = sym.line > 0 ? sym.line - 1 : 0;
        std::string doc;
        for (auto it = comments.rbegin(); it != comments.rend() && want > 0; ++it) {
            if (it->end_line > want) continue;
            if (it->end_line != want) break;
            if (!doc.empty()) doc.insert(0, "\n");
            doc.insert(0, it->text);
            want = it->start_line > 0 ? it->start_line - 1 : 0;
        }
        sym.doc = std::move(doc);
    };

    auto emit = [&](std::string signature, std::size_t line, std::size_t depth) {
        symbol sym;
        sym.signature = std::move(signature);
        sym.line = line;
        sym.depth = depth;
        return sym;
    };

    std::size_t i = 0;
    while (i < code.size()) {
        char const c = code[i];
        if (!in_stmt) {
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++i;
                continue;
            }
            if (c == '#') {
                std::size_t end = i;
                while (end < code.size()) {
                    std::size_t const nl = code.find('\n', end);
                    if (nl == std::string_view::npos) {
                        end = code.size();
                        break;
                    }
                    bool const continued = nl > 0 && code[nl - 1] == '\\';
                    end = nl;
                    if (!continued) break;
                    ++end;
                }
                std::string const directive = normalize(text.substr(i, end - i));
                if (directive.starts_with("#include")) {
                    result.includes.push_back(directive);
                } else if (directive.starts_with("#define")) {
                    result.macros.push_back(directive);
                }
                i = end;
                continue;
            }
            if (c == '}') {
                if (!container_depth.empty()) {
                    symbol sym;
                    sym.close = true;
                    sym.depth = container_depth.size() - 1;
                    sym.line = line_of(i);
                    result.symbols.push_back(std::move(sym));
                    container_depth.pop_back();
                }
                ++i;
                continue;
            }
            in_stmt = true;
            stmt_start = i;
        }

        if (c == '(') {
            ++paren;
            ++i;
            continue;
        }
        if (c == ')') {
            if (paren > 0) --paren;
            ++i;
            continue;
        }
        if (c == '[') {
            ++bracket;
            ++i;
            continue;
        }
        if (c == ']') {
            if (bracket > 0) --bracket;
            ++i;
            continue;
        }

        bool const top = paren == 0 && bracket == 0;

        if (c == ':' && top && (i == 0 || code[i - 1] != ':') && (i + 1 >= code.size() || code[i + 1] != ':')) {
            std::string const head = normalize(text.substr(stmt_start, i - stmt_start));
            if (is_access(head)) {
                symbol sym = emit(head, line_of(stmt_start), container_depth.size());
                sym.kind = "access";
                result.symbols.push_back(std::move(sym));
                in_stmt = false;
            }
            ++i;
            continue;
        }

        if (c == '{' && top) {
            std::string header = normalize(text.substr(stmt_start, i - stmt_start));
            if (header.ends_with('=')) {
                // A brace initializer (`= { … }`): not a declaration.
                i = skip_brace(code, i);
                in_stmt = false;
                continue;
            }
            symbol sym = emit(header, line_of(stmt_start), container_depth.size());
            classify(sym, true);
            if (sym.kind == "enum") {
                std::size_t end = skip_brace(code, i);
                while (end < code.size() && std::isspace(static_cast<unsigned char>(code[end])) != 0) ++end;
                if (end < code.size() && code[end] == ';') ++end;
                sym.signature = normalize(text.substr(stmt_start, end - stmt_start));
                attach_doc(sym);
                result.symbols.push_back(std::move(sym));
                i = end;
                in_stmt = false;
                continue;
            }
            attach_doc(sym);
            bool const container = sym.container;
            result.symbols.push_back(std::move(sym));
            if (container) {
                container_depth.push_back(container_depth.size());
                ++i;
            } else {
                i = skip_brace(code, i);
            }
            in_stmt = false;
            continue;
        }

        if (c == ';' && top) {
            std::string const signature = normalize(text.substr(stmt_start, i - stmt_start));
            if (!signature.empty()) {
                symbol sym = emit(signature, line_of(stmt_start), container_depth.size());
                classify(sym, false);
                attach_doc(sym);
                result.symbols.push_back(std::move(sym));
            }
            in_stmt = false;
            ++i;
            continue;
        }

        ++i;
    }

    return result;
}

std::string render(scan_result const& result, int level) {
    std::string out;
    if (!result.includes.empty()) {
        std::size_t const cap = level >= 3 ? 20 : result.includes.size();
        std::size_t const keep = std::min(cap, result.includes.size());
        for (std::size_t i = 0; i < keep; ++i) {
            out += result.includes[i];
            out.push_back('\n');
        }
        if (keep < result.includes.size()) {
            out += "// … " + std::to_string(result.includes.size() - keep) + " more includes\n";
        }
        out.push_back('\n');
    }
    if (level <= 2 && !result.macros.empty()) {
        for (auto const& macro : result.macros) {
            out += macro;
            out.push_back('\n');
        }
        out.push_back('\n');
    }
    for (auto const& sym : result.symbols) {
        std::string const indent(sym.depth * 2, ' ');
        if (sym.close) {
            out += indent + "}\n";
            continue;
        }
        if (level <= 1 && !sym.doc.empty()) {
            for (auto const& line : text::split_lines(sym.doc)) {
                out += indent + line + "\n";
            }
        }
        std::string line = indent + sym.signature;
        if (sym.container) {
            line += " {";
        } else if (sym.has_body) {
            line += " { … }";
        } else if (!sym.verbatim) {
            line += sym.kind == "access" ? ":" : ";";
        }
        out += line;
        out.push_back('\n');
    }
    return out;
}

} // namespace prompt::compress::cpp
