#include "document.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace prompt::extract {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return text;
}

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char const c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

bool is_space(char c) { return c == ' ' || c == '\t'; }

// Keyword sets `infer_family` tests a line against.
constexpr auto kDockerKeywords = std::to_array<std::string_view>({"RUN", "COPY", "ADD", "CMD", "ENTRYPOINT"});
constexpr auto kShellCommands =
    std::to_array<std::string_view>({"sudo", "export", "apt", "pacman", "cd", "echo", "mkdir", "rm", "cp", "mv"});

// --- Language families ------------------------------------------------------

struct family_def {
    std::string_view name;
    std::span<std::string_view const> aliases;

    constexpr family_def(std::string_view family, std::span<std::string_view const> names)
        : name(family), aliases(names) {}
};

template <std::size_t N>
constexpr std::span<std::string_view const> aliases(std::array<std::string_view, N> const& list) {
    return list;
}

std::unordered_map<std::string, std::string> const& alias_table() {
    static constexpr auto kDiff = std::to_array<std::string_view>({"diff", "patch", "udiff", "unified", "gitdiff"});
    static constexpr auto kCpp = std::to_array<std::string_view>(
        {"cpp", "c++", "cxx", "cc", "hh", "hpp", "hxx", "c", "h", "objc", "objective-c", "cplusplus"});
    static constexpr auto kSh = std::to_array<std::string_view>({
        "sh",
        "bash",
        "shell",
        "zsh",
        "console",
        "shell-session",
        "sh-session",
        "fish",
        "ksh",
        "dash",
        "posix",
        "shellscript",
        "terminal",
    });
    static constexpr auto kPython = std::to_array<std::string_view>({"python", "py", "python3", "py3", "python2"});
    static constexpr auto kJs =
        std::to_array<std::string_view>({"js", "javascript", "node", "mjs", "cjs", "jsx", "ecmascript"});
    static constexpr auto kTs = std::to_array<std::string_view>({"ts", "typescript", "tsx"});
    static constexpr auto kJson = std::to_array<std::string_view>({"json", "jsonc", "json5", "geojson"});
    static constexpr auto kYaml = std::to_array<std::string_view>({"yaml", "yml"});
    static constexpr auto kToml =
        std::to_array<std::string_view>({"toml", "ini", "cfg", "conf", "properties", "dotenv", "env"});
    static constexpr auto kCsv = std::to_array<std::string_view>({"csv", "tsv", "psv"});
    static constexpr auto kMd = std::to_array<std::string_view>({"md", "markdown", "mdx", "commonmark"});
    static constexpr auto kHtml = std::to_array<std::string_view>({"html", "htm", "xhtml", "vue", "svelte"});
    static constexpr auto kXml = std::to_array<std::string_view>({"xml", "xsd", "xsl", "plist", "svg"});
    static constexpr auto kCss = std::to_array<std::string_view>({"css", "scss", "sass", "less", "stylus"});
    static constexpr auto kSql =
        std::to_array<std::string_view>({"sql", "postgres", "psql", "mysql", "sqlite", "plsql"});
    static constexpr auto kRust = std::to_array<std::string_view>({"rust", "rs"});
    static constexpr auto kGo = std::to_array<std::string_view>({"go", "golang"});
    static constexpr auto kJava = std::to_array<std::string_view>({"java"});
    static constexpr auto kKotlin = std::to_array<std::string_view>({"kotlin", "kt", "kts"});
    static constexpr auto kRuby = std::to_array<std::string_view>({"ruby", "rb", "erb"});
    static constexpr auto kPhp = std::to_array<std::string_view>({"php"});
    static constexpr auto kPerl = std::to_array<std::string_view>({"perl", "pl", "pm"});
    static constexpr auto kLua = std::to_array<std::string_view>({"lua"});
    static constexpr auto kR = std::to_array<std::string_view>({"r", "rscript"});
    static constexpr auto kSwift = std::to_array<std::string_view>({"swift"});
    static constexpr auto kScala = std::to_array<std::string_view>({"scala", "sbt"});
    static constexpr auto kCsharp = std::to_array<std::string_view>({"csharp", "cs", "c#", "dotnet"});
    static constexpr auto kMake = std::to_array<std::string_view>({"make", "makefile", "mk", "gnumake"});
    static constexpr auto kCmake = std::to_array<std::string_view>({"cmake", "cmake-include"});
    static constexpr auto kDocker = std::to_array<std::string_view>({"docker", "dockerfile", "containerfile"});
    static constexpr auto kText =
        std::to_array<std::string_view>({"text", "txt", "plain", "plaintext", "none", "output", "log"});
    static constexpr auto kMermaid = std::to_array<std::string_view>({"mermaid"});
    static constexpr auto kTex = std::to_array<std::string_view>({"tex", "latex"});
    static constexpr auto kPowershell = std::to_array<std::string_view>({"powershell", "ps1", "pwsh", "ps"});
    static constexpr auto kAsm = std::to_array<std::string_view>({"asm", "nasm", "gas", "s", "arm", "x86asm"});
    static constexpr auto kBatch = std::to_array<std::string_view>({"batch", "bat", "cmd"});
    static constexpr auto kVba = std::to_array<std::string_view>({"vba", "vb", "visualbasic"});
    static constexpr auto kSolidity = std::to_array<std::string_view>({"solidity", "sol"});
    static constexpr auto kWasm = std::to_array<std::string_view>({"wasm", "wat"});
    static constexpr auto kTerraform = std::to_array<std::string_view>({"terraform", "tf", "hcl"});
    static constexpr auto kNix = std::to_array<std::string_view>({"nix"});
    static constexpr auto kProto = std::to_array<std::string_view>({"proto", "protobuf"});
    static constexpr auto kGraphql = std::to_array<std::string_view>({"graphql", "gql"});
    static constexpr auto kNginx = std::to_array<std::string_view>({"nginx"});
    static constexpr auto kApache = std::to_array<std::string_view>({"apache", "httpd"});
    static constexpr auto kSystemd = std::to_array<std::string_view>({"systemd", "service", "unit"});
    static constexpr auto kNim = std::to_array<std::string_view>({"nim"});
    static constexpr auto kZig = std::to_array<std::string_view>({"zig"});
    static constexpr auto kDart = std::to_array<std::string_view>({"dart"});
    static constexpr auto kElixir = std::to_array<std::string_view>({"elixir", "ex", "exs"});
    static constexpr auto kErlang = std::to_array<std::string_view>({"erlang", "erl"});
    static constexpr auto kHaskell = std::to_array<std::string_view>({"haskell", "hs"});
    static constexpr auto kOcaml = std::to_array<std::string_view>({"ocaml", "ml"});
    static constexpr auto kClojure = std::to_array<std::string_view>({"clojure", "clj"});
    static constexpr auto kLisp = std::to_array<std::string_view>({"lisp", "elisp", "scheme", "racket"});
    static constexpr auto kJulia = std::to_array<std::string_view>({"julia", "jl"});
    static constexpr auto kGroovy = std::to_array<std::string_view>({"groovy"});
    static constexpr auto kFortran = std::to_array<std::string_view>({"fortran", "f90"});
    static constexpr auto kMatlab = std::to_array<std::string_view>({"matlab"});
    static constexpr auto kVerilog = std::to_array<std::string_view>({"verilog", "vhdl"});
    static constexpr auto kGlsl = std::to_array<std::string_view>({"glsl", "hlsl"});
    static constexpr auto kRegex = std::to_array<std::string_view>({"regex", "regexp"});

    static constexpr auto kFamilies = std::to_array<family_def>({
        {"diff", aliases(kDiff)},       {"cpp", aliases(kCpp)},
        {"sh", aliases(kSh)},           {"python", aliases(kPython)},
        {"js", aliases(kJs)},           {"ts", aliases(kTs)},
        {"json", aliases(kJson)},       {"yaml", aliases(kYaml)},
        {"toml", aliases(kToml)},       {"csv", aliases(kCsv)},
        {"md", aliases(kMd)},           {"html", aliases(kHtml)},
        {"xml", aliases(kXml)},         {"css", aliases(kCss)},
        {"sql", aliases(kSql)},         {"rust", aliases(kRust)},
        {"go", aliases(kGo)},           {"java", aliases(kJava)},
        {"kotlin", aliases(kKotlin)},   {"ruby", aliases(kRuby)},
        {"php", aliases(kPhp)},         {"perl", aliases(kPerl)},
        {"lua", aliases(kLua)},         {"r", aliases(kR)},
        {"swift", aliases(kSwift)},     {"scala", aliases(kScala)},
        {"csharp", aliases(kCsharp)},   {"make", aliases(kMake)},
        {"cmake", aliases(kCmake)},     {"docker", aliases(kDocker)},
        {"text", aliases(kText)},       {"mermaid", aliases(kMermaid)},
        {"tex", aliases(kTex)},         {"powershell", aliases(kPowershell)},
        {"asm", aliases(kAsm)},         {"batch", aliases(kBatch)},
        {"vba", aliases(kVba)},         {"solidity", aliases(kSolidity)},
        {"wasm", aliases(kWasm)},       {"terraform", aliases(kTerraform)},
        {"nix", aliases(kNix)},         {"proto", aliases(kProto)},
        {"graphql", aliases(kGraphql)}, {"nginx", aliases(kNginx)},
        {"apache", aliases(kApache)},   {"systemd", aliases(kSystemd)},
        {"nim", aliases(kNim)},         {"zig", aliases(kZig)},
        {"dart", aliases(kDart)},       {"elixir", aliases(kElixir)},
        {"erlang", aliases(kErlang)},   {"haskell", aliases(kHaskell)},
        {"ocaml", aliases(kOcaml)},     {"clojure", aliases(kClojure)},
        {"lisp", aliases(kLisp)},       {"julia", aliases(kJulia)},
        {"groovy", aliases(kGroovy)},   {"fortran", aliases(kFortran)},
        {"matlab", aliases(kMatlab)},   {"verilog", aliases(kVerilog)},
        {"glsl", aliases(kGlsl)},       {"regex", aliases(kRegex)},
    });

    static std::unordered_map<std::string, std::string> const table = [] {
        std::unordered_map<std::string, std::string> out;
        out.reserve(200);
        for (auto const& entry : kFamilies) {
            for (auto alias : entry.aliases) out.emplace(std::string(alias), std::string(entry.name));
        }
        return out;
    }();
    return table;
}

bool starts_with_ci(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (starts_with_ci(haystack.substr(i), needle)) return true;
    }
    return false;
}

// A line-oriented predicate over `text`.
template <typename Fn>
bool any_line(std::string_view text, Fn&& predicate) {
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find('\n', begin);
        auto const line = text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin);
        if (predicate(line)) return true;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return false;
}

bool looks_csv(std::string_view text) {
    std::vector<std::string_view> rows;
    std::size_t begin = 0;
    while (begin <= text.size() && rows.size() < 6) {
        auto const end = text.find('\n', begin);
        auto const line = text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin);
        if (!trim(line).empty()) rows.push_back(line);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    if (rows.size() < 2) return false;
    std::optional<std::size_t> count;
    for (auto row : rows) {
        if (!row.contains(',')) return false;
        std::size_t fields = 1;
        for (char const c : row) {
            if (c == ',') ++fields;
        }
        if (count.has_value() && *count != fields) return false;
        count = fields;
    }
    return count.has_value() && *count >= 2;
}

// --- Fence / heading / table grammar ----------------------------------------

std::size_t consume_lead(std::string_view line, std::size_t from) {
    std::size_t i = from;
    for (;;) {
        std::size_t j = i;
        while (j < line.size() && is_space(line[j])) ++j;
        if (j < line.size() && line[j] == '>') {
            ++j;
            if (j < line.size() && is_space(line[j])) ++j;
            i = j;
            continue;
        }
        return j;
    }
}

struct fence_open {
    std::string lead;
    std::string fence;
    std::string info;
};

std::optional<fence_open> match_fence_open(std::string_view line) {
    std::size_t const start = consume_lead(line, 0);
    if (start >= line.size()) return std::nullopt;
    char const c = line[start];
    if (c != '`' && c != '~') return std::nullopt;
    std::size_t n = 0;
    while (start + n < line.size() && line[start + n] == c) ++n;
    if (n < 3) return std::nullopt;
    std::string const info(line.substr(start + n));
    if (c == '`' && info.contains('`')) return std::nullopt;
    return fence_open{.lead = std::string(line.substr(0, start)), .fence = std::string(n, c), .info = info};
}

std::optional<std::pair<char, std::size_t>> match_fence_close(std::string_view line) {
    std::size_t const start = consume_lead(line, 0);
    if (start >= line.size()) return std::nullopt;
    char const c = line[start];
    if (c != '`' && c != '~') return std::nullopt;
    std::size_t n = 0;
    while (start + n < line.size() && line[start + n] == c) ++n;
    if (n < 3) return std::nullopt;
    for (std::size_t i = start + n; i < line.size(); ++i) {
        if (!is_space(line[i])) return std::nullopt;
    }
    return std::pair{c, n};
}

struct heading_match {
    int level = 0;
    std::string text;
};

std::optional<heading_match> match_heading(std::string_view line) {
    std::size_t const start = consume_lead(line, 0);
    std::size_t n = 0;
    while (start + n < line.size() && line[start + n] == '#' && n < 6) ++n;
    if (n == 0) return std::nullopt;
    if (start + n >= line.size() || !is_space(line[start + n])) return std::nullopt;
    std::string text(line.substr(start + n + 1));
    while (!text.empty() && is_space(text.back())) text.pop_back();
    // Strip a closing `#` run that is preceded by whitespace.
    std::size_t e = text.size();
    while (e > 0 && text[e - 1] == '#') --e;
    if (e < text.size() && e > 0 && is_space(text[e - 1])) {
        std::size_t w = e;
        while (w > 0 && is_space(text[w - 1])) --w;
        text.resize(w);
    }
    return heading_match{.level = static_cast<int>(n), .text = std::string(trim(text))};
}

bool is_table_delimiter(std::string_view line) {
    if (!line.contains('|')) return false;
    std::string_view text = trim(line);
    if (!text.empty() && text.front() == '|') text.remove_prefix(1);
    if (!text.empty() && text.back() == '|') text.remove_suffix(1);
    bool any = false;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find('|', begin);
        auto cell = trim(text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin));
        if (!cell.empty() && cell.front() == ':') cell.remove_prefix(1);
        if (!cell.empty() && cell.back() == ':') cell.remove_suffix(1);
        if (cell.empty()) return false;
        for (char const c : cell) {
            if (c != '-') return false;
        }
        any = true;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return any;
}

std::string strip_quote(std::string_view line) {
    std::size_t i = 0;
    for (;;) {
        std::size_t j = i;
        while (j < line.size() && is_space(line[j])) ++j;
        if (j < line.size() && line[j] == '>') {
            ++j;
            if (j < line.size() && is_space(line[j])) ++j;
            i = j;
            continue;
        }
        break;
    }
    return std::string(line.substr(i));
}

std::string dedent(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find('\n', begin);
        lines.push_back(text.substr(begin, (end == std::string::npos ? text.size() : end) - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    std::string common;
    bool first = true;
    for (auto line : lines) {
        if (trim(line).empty()) continue;
        std::size_t n = 0;
        while (n < line.size() && is_space(line[n])) ++n;
        std::string_view indent = line.substr(0, n);
        if (first) {
            common = std::string(indent);
            first = false;
            continue;
        }
        std::size_t match = 0;
        while (match < common.size() && match < indent.size() && common[match] == indent[match]) ++match;
        common.resize(match);
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto line = lines[i];
        if (line.starts_with(common)) line.remove_prefix(common.size());
        out += line;
        if (i + 1 < lines.size()) out.push_back('\n');
    }
    return out;
}

std::string normalize_body(std::vector<std::string> const& lines, std::size_t quote_depth) {
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line = lines[i];
        for (std::size_t depth = 0; depth < quote_depth; ++depth) {
            std::size_t j = 0;
            while (j < line.size() && is_space(line[j])) ++j;
            if (j < line.size() && line[j] == '>') {
                ++j;
                if (j < line.size() && is_space(line[j])) ++j;
                line = line.substr(j);
            } else {
                break;
            }
        }
        joined += line;
        if (i + 1 < lines.size()) joined.push_back('\n');
    }
    std::string out = dedent(joined);
    while (!out.empty() && out.front() == '\n') out.erase(out.begin());
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

void parse_info(std::string_view info, std::string& lang, std::string& title, std::string& ident) {
    auto raw = std::string(trim(info));
    if (raw.size() >= 2 && raw.front() == '{' && raw.back() == '}') {
        std::string_view body(raw.data() + 1, raw.size() - 2);
        std::size_t begin = 0;
        while (begin < body.size()) {
            while (begin < body.size() && is_space(body[begin])) ++begin;
            auto end = begin;
            while (end < body.size() && !is_space(body[end])) ++end;
            auto token = body.substr(begin, end - begin);
            begin = end;
            if (token.empty()) continue;
            if (token.front() == '.') {
                if (lang.empty()) lang = std::string(token.substr(1));
            } else if (token.front() == '#') {
                if (ident.empty()) ident = std::string(token.substr(1));
            } else if (auto const eq = token.find('='); eq != std::string_view::npos) {
                auto key = lower(token.substr(0, eq));
                auto value = token.substr(eq + 1);
                if (value.size() >= 2 && value.front() == value.back() &&
                    (value.front() == '"' || value.front() == '\''))
                    value = value.substr(1, value.size() - 2);
                if ((key == "title" || key == "name" || key == "file") && title.empty()) title = std::string(value);
            } else if (lang.empty()) {
                lang = std::string(token);
            }
        }
        return;
    }
    // `key=value` attributes outside braces; the first bare word is the language.
    std::string rest;
    std::size_t i = 0;
    std::string_view text = trim(raw);
    while (i < text.size()) {
        auto sp = text.find_first_of(" \t", i);
        auto token = text.substr(i, (sp == std::string_view::npos ? text.size() : sp) - i);
        i = sp == std::string_view::npos ? text.size() : sp + 1;
        if (token.empty()) continue;
        if (auto const eq = token.find('='); eq != std::string_view::npos) {
            auto key = lower(token.substr(0, eq));
            auto value = token.substr(eq + 1);
            if (value.size() >= 2 && value.front() == value.back() && (value.front() == '"' || value.front() == '\''))
                value = value.substr(1, value.size() - 2);
            if ((key == "title" || key == "name" || key == "file") && title.empty()) title = std::string(value);
        } else if (lang.empty()) {
            lang = std::string(token);
        }
    }
    (void)rest;
}

} // namespace

std::string family(std::string_view lang) {
    auto key = lower(trim(lang));
    auto const space = key.find_first_of(" \t");
    if (space != std::string::npos) key.resize(space);
    if (key.empty()) return {};
    if (auto const it = alias_table().find(key); it != alias_table().end()) return it->second;
    return key;
}

std::string infer_family(std::string_view body) {
    std::string text(trim(body));
    if (text.empty()) return {};
    std::string head = text.substr(0, std::min<std::size_t>(4000, text.size()));
    auto const first_end = head.find('\n');
    std::string first(head.substr(0, first_end == std::string::npos ? head.size() : first_end));
    first = std::string(trim(first));

    if (any_line(head, [](std::string_view l) { return l.starts_with("diff --git "); }) ||
        (any_line(head,
                  [](std::string_view l) {
                      if (!l.starts_with("@@ -")) return false;
                      return !l.empty() && l.size() > 4 && std::isdigit(static_cast<unsigned char>(l[4])) != 0;
                  }) &&
         any_line(head, [](std::string_view l) { return l.starts_with("--- ") || l.starts_with("+++ "); }))) {
        return "diff";
    }
    if (!first.empty() && (first.front() == '{' || first.front() == '[')) {
        auto parsed = nlohmann::json::parse(text, nullptr, false);
        if (!parsed.is_discarded()) return "json";
    }
    if (any_line(head,
                 [](std::string_view l) {
                     if (!l.starts_with("#include")) return false;
                     std::string_view rest = l.substr(8);
                     while (!rest.empty() && is_space(rest.front())) rest.remove_prefix(1);
                     return !rest.empty() && (rest.front() == '<' || rest.front() == '"');
                 }) ||
        head.contains("std::")) {
        return "cpp";
    }
    if (first.starts_with("#!")) {
        auto lowered = lower(first);
        for (auto const* name : {"python", "node", "perl", "ruby", "php", "lua"}) {
            if (lowered.contains(name)) return name;
        }
        return "sh";
    }
    if (starts_with_ci(first, "<!DOCTYPE html") || starts_with_ci(first, "<html")) return "html";
    if (looks_csv(head)) return "csv";
    {
        auto lowered = lower(first);
        bool const sql_head = lowered.starts_with("select") || lowered.starts_with("insert into") ||
                              lowered.starts_with("update") || lowered.starts_with("delete from") ||
                              lowered.starts_with("create table") || lowered.starts_with("with");
        if (sql_head && (contains_ci(head, "from") || contains_ci(head, "where") || contains_ci(head, "values")))
            return "sql";
    }
    if (any_line(head,
                 [](std::string_view l) {
                     if (!l.starts_with("package ")) return false;
                     auto rest = trim(l.substr(8));
                     return !rest.empty() && std::ranges::all_of(rest, [](char c) {
                         auto const uc = static_cast<unsigned char>(c);
                         return std::isalnum(uc) != 0 || c == '/' || c == '_';
                     });
                 }) &&
        any_line(head, [](std::string_view l) { return l.starts_with("func "); })) {
        return "go";
    }
    if (any_line(head,
                 [](std::string_view l) {
                     auto t = trim(l);
                     return t.starts_with("use ") && t.ends_with(';');
                 }) &&
        head.contains("fn main(")) {
        return "rust";
    }
    if (any_line(head, [](std::string_view l) { return l.starts_with("cmake_minimum_required"); }) ||
        any_line(head, [](std::string_view l) {
            return l.starts_with("add_executable(") || l.starts_with("target_link_libraries(");
        })) {
        return "cmake";
    }
    if (any_line(head, [](std::string_view l) { return l.starts_with("FROM "); }) &&
        any_line(head, [](std::string_view l) {
            return std::ranges::any_of(kDockerKeywords, [l](std::string_view kw) {
                return l.starts_with(kw) && l.size() > kw.size() && is_space(l[kw.size()]);
            });
        })) {
        return "docker";
    }
    if (any_line(head, [](std::string_view l) { return l.starts_with("using System"); })) return "csharp";
    if (any_line(head, [](std::string_view l) {
            auto t = trim(l);
            return (t.starts_with("public ") || t.starts_with("private ")) &&
                   (t.contains("class ") || t.contains("interface ") || t.contains("enum "));
        })) {
        return "java";
    }
    if (any_line(head,
                 [](std::string_view l) {
                     auto t = trim(l);
                     return (t.starts_with("def ") || t.starts_with("class ")) && t.ends_with(':');
                 }) ||
        any_line(head, [](std::string_view l) {
            auto t = trim(l);
            return t.starts_with("import ") || t.starts_with("from ");
        })) {
        return "python";
    }
    if (any_line(head,
                 [](std::string_view l) {
                     return std::ranges::any_of(kShellCommands, [l](std::string_view kw) {
                         return l.starts_with(kw) && (l.size() == kw.size() || is_space(l[kw.size()]));
                     });
                 }) ||
        any_line(head, [](std::string_view l) { return l.starts_with("$ "); })) {
        return "sh";
    }
    if (head.contains("```")) return "md";
    return {};
}

void assign_indices(std::vector<block>& blocks) {
    static constexpr std::array<std::string_view, 6> order = {
        "code", "table", "section", "frontmatter", "command", "field",
    };
    auto rank = [](std::string_view kind) {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == kind) return i;
        }
        return order.size();
    };
    std::stable_sort(blocks.begin(), blocks.end(), [&](block const& a, block const& b) {
        if (a.file_order != b.file_order) return a.file_order < b.file_order;
        if (a.start_line != b.start_line) return a.start_line < b.start_line;
        return rank(a.kind) < rank(b.kind);
    });
    std::unordered_map<std::string, std::size_t> counters;
    for (auto& b : blocks) b.index = ++counters[b.kind];
}

std::vector<block> parse_document(std::string_view text, std::string_view name, int file_order, parse_options options) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            normalized.push_back('\n');
            continue;
        }
        normalized.push_back(text[i]);
    }
    if (normalized.starts_with("\xEF\xBB\xBF")) normalized.erase(0, 3);

    std::vector<std::string_view> lines;
    {
        std::size_t begin = 0;
        while (begin <= normalized.size()) {
            auto const end = normalized.find('\n', begin);
            lines.push_back(std::string_view(normalized)
                                .substr(begin, (end == std::string::npos ? normalized.size() : end) - begin));
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }
    auto const total = lines.size();

    std::vector<block> blocks;
    struct paragraph {
        std::size_t start;
        std::size_t end;
        std::string text;
    };
    struct heading {
        int level;
        std::string text;
        std::size_t line;
        std::vector<std::pair<int, std::string>> ancestors;
    };
    std::vector<paragraph> paragraphs;
    std::vector<heading> headings;
    std::vector<std::size_t> struct_starts;
    std::vector<std::pair<int, std::string>> stack;
    std::vector<std::string> buffer;
    std::size_t buffer_start = 0;
    std::string last_prose;

    auto flush = [&](std::size_t end_line) {
        if (buffer.empty()) return;
        std::string joined;
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            joined += strip_quote(buffer[i]);
            if (i + 1 < buffer.size()) joined.push_back('\n');
        }
        while (!joined.empty() && joined.front() == '\n') joined.erase(joined.begin());
        while (!joined.empty() && joined.back() == '\n') joined.pop_back();
        paragraphs.push_back(paragraph{.start = buffer_start, .end = end_line, .text = joined});
        if (!joined.empty()) last_prose = joined;
        buffer.clear();
    };

    auto add = [&](block b) {
        b.file_order = file_order;
        b.before = last_prose;
        b.lang = family(b.lang_raw);
        struct_starts.push_back(b.start_line);
        blocks.push_back(std::move(b));
    };

    std::size_t i = 0;
    while (i < total) {
        std::string_view const line = lines[i];
        std::size_t const lineno = i + 1;

        if (i == 0) {
            auto stripped = trim(line);
            if (stripped == "---" || stripped == "+++") {
                std::optional<std::size_t> close;
                for (std::size_t j = 1; j < total; ++j) {
                    if (trim(lines[j]) == stripped) {
                        close = j;
                        break;
                    }
                }
                if (close.has_value()) {
                    std::string body;
                    for (std::size_t j = 1; j < *close; ++j) {
                        body += lines[j];
                        if (j + 1 < *close) body.push_back('\n');
                    }
                    block front;
                    front.kind = "frontmatter";
                    front.file = std::string(name);
                    front.start_line = 1;
                    front.end_line = *close + 1;
                    front.body = std::string(trim(body));
                    front.lang_raw = stripped == "---" ? "yaml" : "toml";
                    front.info_raw = std::string(stripped);
                    add(std::move(front));
                    struct_starts.push_back(*close + 1);
                    last_prose.clear();
                    i = *close + 1;
                    continue;
                }
            }
        }

        if (auto fence = match_fence_open(line); fence.has_value()) {
            flush(i);
            char const character = fence->fence.front();
            std::size_t const marker_len = fence->fence.size();
            std::vector<std::string> content;
            std::size_t j = i + 1;
            bool closed = false;
            while (j < total) {
                if (auto closing = match_fence_close(lines[j]);
                    closing.has_value() && closing->first == character && closing->second >= marker_len) {
                    closed = true;
                    break;
                }
                content.emplace_back(lines[j]);
                ++j;
            }
            std::string lang_raw;
            std::string title;
            std::string ident;
            parse_info(fence->info, lang_raw, title, ident);
            std::size_t const quote_depth =
                static_cast<std::size_t>(std::count(fence->lead.begin(), fence->lead.end(), '>'));
            block code;
            code.kind = "code";
            code.file = std::string(name);
            code.start_line = lineno;
            code.end_line = closed ? j + 1 : total;
            code.body = normalize_body(content, quote_depth);
            code.lang_raw = lang_raw;
            code.info_raw = std::string(fence->info);
            code.fence = fence->fence;
            code.title = title;
            code.ident = ident;
            code.heading_path = stack;
            code.closed = closed;
            add(std::move(code));
            i = j + 1;
            continue;
        }

        if (auto hit = match_heading(line); hit.has_value()) {
            flush(i);
            last_prose.clear();
            while (!stack.empty() && stack.back().first >= hit->level) stack.pop_back();
            headings.push_back(heading{.level = hit->level, .text = hit->text, .line = lineno, .ancestors = stack});
            stack.emplace_back(hit->level, hit->text);
            struct_starts.push_back(lineno);
            ++i;
            continue;
        }

        if (!trim(line).empty() && line.contains('|') && i + 1 < total && is_table_delimiter(lines[i + 1])) {
            flush(i);
            std::size_t j = i + 2;
            while (j < total && !trim(lines[j]).empty() && lines[j].contains('|')) ++j;
            std::string body;
            for (std::size_t k = i; k < j; ++k) {
                body += lines[k];
                if (k + 1 < j) body.push_back('\n');
            }
            block table;
            table.kind = "table";
            table.file = std::string(name);
            table.start_line = lineno;
            table.end_line = j;
            table.body = std::string(trim(body));
            table.heading_path = stack;
            add(std::move(table));
            last_prose.clear();
            i = j;
            continue;
        }

        if ((line.starts_with("    ") || line.starts_with('\t')) && (i == 0 || trim(lines[i - 1]).empty())) {
            std::size_t j = i;
            std::vector<std::string> content;
            while (j < total) {
                std::string_view current = lines[j];
                if (trim(current).empty()) {
                    content.emplace_back();
                    ++j;
                } else if (current.starts_with("    ")) {
                    content.emplace_back(current.substr(4));
                    ++j;
                } else if (current.starts_with('\t')) {
                    content.emplace_back(current.substr(1));
                    ++j;
                } else {
                    break;
                }
            }
            std::string body;
            for (std::size_t k = 0; k < content.size(); ++k) {
                body += content[k];
                if (k + 1 < content.size()) body.push_back('\n');
            }
            body = std::string(trim(body));
            if (!body.empty()) {
                flush(i);
                if (options.indented) {
                    block code;
                    code.kind = "code";
                    code.file = std::string(name);
                    code.start_line = lineno;
                    code.end_line = j;
                    code.body = body;
                    code.heading_path = stack;
                    add(std::move(code));
                }
                struct_starts.push_back(lineno);
                last_prose.clear();
                i = j;
                continue;
            }
        }

        if (trim(line).empty()) {
            flush(i);
            ++i;
            continue;
        }

        if (buffer.empty()) buffer_start = lineno;
        buffer.emplace_back(line);
        ++i;
    }
    flush(total);

    for (std::size_t position = 0; position < headings.size(); ++position) {
        auto const& h = headings[position];
        std::size_t stop = total;
        for (std::size_t later = position + 1; later < headings.size(); ++later) {
            if (options.include_subsections && headings[later].level > h.level) continue;
            stop = headings[later].line - 1;
            break;
        }
        std::string body;
        if (stop > h.line) {
            for (std::size_t k = h.line; k < stop && k < total; ++k) {
                body += lines[k];
                if (k + 1 < stop && k + 1 < total) body.push_back('\n');
            }
        }
        block section;
        section.kind = "section";
        section.file = std::string(name);
        section.start_line = h.line;
        section.end_line = stop >= h.line ? stop : h.line;
        section.body = std::string(trim(body));
        section.title = h.text;
        section.heading_path = h.ancestors;
        section.heading_level = h.level;
        blocks.push_back(std::move(section));
    }

    for (auto& b : blocks) {
        std::optional<std::size_t> next_start;
        for (auto start : struct_starts) {
            if (start > b.end_line && (!next_start.has_value() || start < *next_start)) next_start = start;
        }
        for (auto const& p : paragraphs) {
            if (p.start > b.end_line && (!next_start.has_value() || p.start < *next_start)) {
                b.after = p.text;
                break;
            }
        }
    }
    return blocks;
}

std::vector<std::string> split_table_row(std::string_view line) {
    std::string_view text = trim(line);
    if (!text.empty() && text.front() == '|') text.remove_prefix(1);
    if (!text.empty() && text.back() == '|') text.remove_suffix(1);
    std::vector<std::string> cells;
    std::string current;
    bool escaped = false;
    for (char const c : text) {
        if (escaped) {
            current.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '|') {
            cells.emplace_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    cells.emplace_back(trim(current));
    return cells;
}

namespace {
std::vector<std::string_view> table_lines(block const& b) {
    std::vector<std::string_view> out;
    std::size_t begin = 0;
    while (begin <= b.body.size()) {
        auto const end = b.body.find('\n', begin);
        auto const line =
            std::string_view(b.body).substr(begin, (end == std::string::npos ? b.body.size() : end) - begin);
        if (!trim(line).empty()) out.push_back(line);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return out;
}
} // namespace

std::vector<std::string> table_headers(block const& b) {
    auto const lines = table_lines(b);
    return lines.empty() ? std::vector<std::string>{} : split_table_row(lines.front());
}

std::vector<std::vector<std::string>> table_rows(block const& b) {
    auto const lines = table_lines(b);
    if (lines.size() < 2) return {};
    auto const headers = split_table_row(lines.front());
    std::vector<std::vector<std::string>> rows;
    for (std::size_t i = 2; i < lines.size(); ++i) {
        auto cells = split_table_row(lines[i]);
        cells.resize(headers.size());
        rows.push_back(std::move(cells));
    }
    return rows;
}

std::string command_line(block const& b) {
    if (b.kind != "code" && b.kind != "command") return {};
    if (trim(b.body).empty()) return {};
    std::size_t begin = 0;
    while (begin <= b.body.size()) {
        auto const end = b.body.find('\n', begin);
        auto text = std::string(
            trim(std::string_view(b.body).substr(begin, (end == std::string::npos ? b.body.size() : end) - begin)));
        if (!text.empty() && text.front() != '#') {
            if (text.starts_with("$ ")) text = std::string(trim(std::string_view(text).substr(2)));
            return text;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

std::optional<block> derive_command(block const& b) {
    auto const line = command_line(b);
    if (b.kind != "code" || line.empty()) return std::nullopt;
    std::size_t end = 0;
    while (end < line.size() && !is_space(line[end])) ++end;
    std::string const program = line.substr(0, end);
    auto const slash = program.find_last_of('/');
    block out = b;
    out.kind = "command";
    out.program = slash == std::string::npos ? program : program.substr(slash + 1);
    return out;
}

std::string display_lang(block const& b, bool guess) {
    if (!b.lang.empty()) return b.lang;
    if (guess) {
        auto const guessed = infer_family(b.body);
        if (!guessed.empty()) return guessed + "?";
    }
    return {};
}

std::string nearest_heading(block const& b) {
    if (b.kind == "section") return b.title;
    return b.heading_path.empty() ? std::string{} : b.heading_path.back().second;
}

std::string full_heading(block const& b) {
    std::string out;
    for (auto const& [level, text] : b.heading_path) {
        (void)level;
        if (!out.empty()) out += " > ";
        out += text;
    }
    if (b.kind == "section" && !b.title.empty()) {
        if (!out.empty()) out += " > ";
        out += b.title;
    }
    return out;
}

std::size_t count_lines(std::string_view text) {
    if (text.empty()) return 0;
    std::size_t count = 1;
    for (char const c : text) {
        if (c == '\n') ++count;
    }
    return count;
}

std::string first_line(std::string_view text, std::size_t limit) {
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find('\n', begin);
        auto line = trim(text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin));
        if (!line.empty()) {
            if (line.size() <= limit) return std::string(line);
            return std::string(line.substr(0, std::max<std::size_t>(1, limit - 1))) + "\xE2\x80\xA6";
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return {};
}

} // namespace prompt::extract
