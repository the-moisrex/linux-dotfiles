// md.extract — the standalone face of the extractor registry.
//
// It shares `prompt_lib` with the dispatcher, so `--select`/`--contract`/`--items`
// and `.extract`/`.expect`/`.each` can never disagree: the grammar, the parser
// and the renderers are one implementation. The CLI keeps the flag-based
// selection (`--kind`/`--lang`/`--heading`/…/`-n`) that predates the selector
// language, plus the writing modes (`-o`, `--out-dir`) used in shell pipelines.

#include "core/process.hpp"
#include "extract/common.hpp"
#include "extract/document.hpp"
#include "extract/render.hpp"
#include "sdk/extract.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

using prompt::soft_error;
using prompt::usage_error;
using prompt::extract::block;
using prompt::extract::match;
using prompt::extract::selection;

constexpr std::string_view kProg = "md.extract";

// --- Small helpers ----------------------------------------------------------

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return text;
}

std::string rstrip_newlines(std::string_view text) {
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return std::string(text);
}

std::string join_commas(std::vector<std::string> const& values) {
    std::string out;
    for (auto const& value : values) {
        if (!out.empty()) out += ',';
        out += value;
    }
    return out;
}

std::string join_sorted(std::set<std::string> const& values) {
    std::string out;
    for (auto const& value : values) {
        if (!out.empty()) out += ", ";
        out += value;
    }
    return out;
}

// Python's `json.dumps` default separators (`, ` and `: `); nlohmann's compact
// dump omits the spaces, so re-insert them outside string literals.
std::string spaced_json(nlohmann::ordered_json const& value) {
    std::string const compact = value.dump();
    std::string out;
    out.reserve(compact.size() + (compact.size() / 8));
    bool in_string = false;
    bool escaped = false;
    for (char const c : compact) {
        out.push_back(c);
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == ':' || c == ',') {
            out.push_back(' ');
        }
    }
    return out;
}

// --- Flag-based selection (the pre-selector CLI) ----------------------------

struct query {
    std::set<std::string> kinds;
    std::vector<std::string> langs;
    bool guess = false;
    std::vector<std::regex> heading;
    std::vector<std::regex> under;
    std::vector<std::regex> before;
    std::vector<std::regex> after;
    std::vector<std::regex> title;
    std::vector<std::regex> grep;
};

std::string regex_escape(std::string_view text) {
    std::string out;
    for (char const c : text) {
        if (std::string_view("^$.|?*+()[]{}").contains(c)) out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::vector<std::regex> compile_patterns(std::vector<std::string> const& values, bool literal) {
    std::vector<std::regex> out;
    for (auto const& value : values) {
        try {
            out.emplace_back(literal ? regex_escape(value) : value, std::regex::icase);
        } catch (std::regex_error const& error) {
            throw usage_error{std::format("{}: invalid pattern '{}': {}\n", kProg, value, error.what())};
        }
    }
    return out;
}

template <typename Fn>
bool any_search(std::vector<std::regex> const& patterns, Fn&& text) {
    return std::ranges::any_of(patterns, [&](std::regex const& pattern) { return std::regex_search(text(), pattern); });
}

bool matches_query(block const& b, query const& q) {
    if (!q.kinds.contains(b.kind)) return false;
    if (!q.langs.empty() && b.kind != "code" && b.kind != "frontmatter") return false;
    if (!prompt::extract::lang_matches(b, join_commas(q.langs), q.guess)) return false;
    if (!q.heading.empty() && !any_search(q.heading, [&] { return prompt::extract::nearest_heading(b); })) return false;
    if (!q.under.empty()) {
        bool found = false;
        for (auto const& [level, text] : b.heading_path) {
            (void)level;
            if (any_search(q.under, [&] { return text; })) {
                found = true;
                break;
            }
        }
        if (b.kind == "section" && any_search(q.under, [&] { return b.title; })) found = true;
        if (!found) return false;
    }
    if (!q.before.empty() && !any_search(q.before, [&] { return b.before; })) return false;
    if (!q.after.empty() && !any_search(q.after, [&] { return b.after; })) return false;
    if (!q.title.empty()) {
        std::string haystack = b.title + "\n" + b.ident + "\n" + b.info_raw;
        if (!any_search(q.title, [&] { return haystack; })) return false;
    }
    if (!q.grep.empty() && !any_search(q.grep, [&] { return b.body; })) return false;
    return true;
}

std::vector<int> parse_index_spec(std::string_view raw) {
    std::vector<int> values;
    std::size_t begin = 0;
    while (begin <= raw.size()) {
        auto const end = raw.find(',', begin);
        auto part = trim(raw.substr(begin, (end == std::string_view::npos ? raw.size() : end) - begin));
        begin = end == std::string_view::npos ? raw.size() + 1 : end + 1;
        if (part.empty()) continue;
        static std::regex const ranged(R"((-?\d+)\s*-\s*(-?\d+))");
        static std::regex const single(R"(-?\d+)");
        std::match_results<std::string_view::const_iterator> m;
        if (std::regex_match(part.begin(), part.end(), m, ranged)) {
            int const start = std::stoi(m[1].str());
            int const stop = std::stoi(m[2].str());
            if (start == 0 || stop == 0)
                throw usage_error{std::format("{}: --index is 1-based; 0 is not a block\n", kProg)};
            int const step = stop >= start ? 1 : -1;
            for (int value = start; step > 0 ? value <= stop : value >= stop; value += step) values.push_back(value);
            continue;
        }
        if (std::regex_match(part.begin(), part.end(), single)) {
            int const value = std::stoi(std::string(part));
            if (value == 0) throw usage_error{std::format("{}: --index is 1-based; 0 is not a block\n", kProg)};
            values.push_back(value);
            continue;
        }
        throw usage_error{std::format("{}: invalid --index value '{}' (try 3, 2-5, 1,4 or -1)\n", kProg, part)};
    }
    return values;
}

std::vector<block> select_blocks(std::vector<block> const& blocks, query const& q,
                                 std::optional<std::vector<int>> const& indices) {
    std::vector<block const*> matched;
    for (auto const& b : blocks) {
        if (q.kinds.contains(b.kind) && matches_query(b, q)) matched.push_back(&b);
    }
    if (!indices.has_value() || indices->empty()) {
        std::vector<block> out;
        out.reserve(matched.size());
        for (auto const* b : matched) out.push_back(*b);
        return out;
    }
    std::vector<block> picked;
    std::set<std::size_t> seen;
    for (auto const value : *indices) {
        auto const size = static_cast<long>(matched.size());
        auto const position = value > 0 ? value - 1 : size + value;
        if (position < 0 || position >= size) continue;
        auto const index = static_cast<std::size_t>(position);
        if (seen.insert(index).second) picked.push_back(*matched[index]);
    }
    if (picked.empty()) {
        std::string list;
        for (auto const value : *indices) {
            if (!list.empty()) list += ", ";
            list += std::to_string(value);
        }
        throw soft_error{
            std::format("index {} is out of range (the selection holds {} blocks)\n", list, matched.size())};
    }
    return picked;
}

// --- Rendering --------------------------------------------------------------

prompt::extract::render_options render_options_from(bool with_heading, bool with_intro, bool fence) {
    return {.with_heading = with_heading, .with_intro = with_intro, .fence = fence};
}

std::vector<match> as_matches(std::vector<block> const& blocks) {
    std::vector<match> out;
    out.reserve(blocks.size());
    for (auto const& b : blocks) out.push_back(prompt::extract::match_from(b));
    return out;
}

// --- Writing ----------------------------------------------------------------

std::map<std::string, std::string> const& extensions() {
    static std::map<std::string, std::string> const table = {
        {"cpp", "cpp"},
        {"sh", "sh"},
        {"python", "py"},
        {"js", "js"},
        {"ts", "ts"},
        {"json", "json"},
        {"yaml", "yaml"},
        {"toml", "toml"},
        {"csv", "csv"},
        {"md", "md"},
        {"html", "html"},
        {"xml", "xml"},
        {"css", "css"},
        {"sql", "sql"},
        {"rust", "rs"},
        {"go", "go"},
        {"java", "java"},
        {"kotlin", "kt"},
        {"ruby", "rb"},
        {"php", "php"},
        {"perl", "pl"},
        {"lua", "lua"},
        {"r", "r"},
        {"swift", "swift"},
        {"scala", "scala"},
        {"csharp", "cs"},
        {"make", "mk"},
        {"cmake", "cmake"},
        {"docker", "dockerfile"},
        {"text", "txt"},
        {"mermaid", "mmd"},
        {"tex", "tex"},
        {"powershell", "ps1"},
        {"asm", "asm"},
        {"batch", "bat"},
        {"vba", "vb"},
        {"solidity", "sol"},
        {"wasm", "wat"},
        {"terraform", "tf"},
        {"nix", "nix"},
        {"proto", "proto"},
        {"graphql", "graphql"},
        {"nginx", "conf"},
        {"apache", "conf"},
        {"systemd", "service"},
        {"nim", "nim"},
        {"zig", "zig"},
        {"dart", "dart"},
        {"elixir", "ex"},
        {"erlang", "erl"},
        {"haskell", "hs"},
        {"ocaml", "ml"},
        {"clojure", "clj"},
        {"lisp", "lisp"},
        {"julia", "jl"},
        {"groovy", "groovy"},
        {"fortran", "f90"},
        {"matlab", "m"},
        {"verilog", "v"},
        {"glsl", "glsl"},
        {"regex", "txt"},
        {"diff", "diff"},
    };
    return table;
}

std::string sanitize_name(std::string_view name) {
    std::string out;
    bool pending_dash = false;
    for (char const c : name) {
        auto const uc = static_cast<unsigned char>(c);
        bool const word = std::isalnum(uc) != 0 || c == '_' || uc >= 0x80;
        if (word || c == '.' || c == '@' || c == '+' || c == '-') {
            if (pending_dash && !out.empty()) out.push_back('-');
            pending_dash = false;
            out.push_back(c);
        } else {
            pending_dash = true;
        }
    }
    while (!out.empty() && (out.front() == '.' || out.front() == '-' || out.front() == '_')) out.erase(out.begin());
    while (!out.empty() && (out.back() == '.' || out.back() == '-' || out.back() == '_')) out.pop_back();
    return out.empty() ? "block" : out;
}

std::string stem_of(std::filesystem::path const& path) { return path.stem().string(); }

std::string target_name(match const& m, std::optional<std::string> const& ext_override, bool guess) {
    std::string title = !m.title.empty() ? m.title : m.id;
    std::string stem;
    if (!title.empty()) {
        stem = sanitize_name(std::filesystem::path(title).filename().string());
    } else {
        std::string language = m.lang;
        if (language.empty() && guess) language = prompt::extract::infer_family(m.source);
        stem = std::format("{}-{}", language.empty() ? m.kind : language, m.index);
    }
    if (ext_override.has_value()) return std::format("{}.{}", stem_of(stem), *ext_override);
    if (!std::filesystem::path(stem).has_extension()) {
        std::string language = m.lang;
        if (language.empty() && guess) language = prompt::extract::infer_family(m.source);
        auto const it = extensions().find(language);
        std::string extension = it != extensions().end() ? it->second : std::string{};
        if (extension.empty()) {
            if (m.kind == "frontmatter") extension = "yaml";
            else if (m.kind == "section" || m.kind == "table")
                extension = "md";
            else
                extension = "txt";
        }
        stem += "." + extension;
    }
    return stem;
}

void write_matches(std::vector<match> const& matches, bool has_output, std::string const& output, bool has_out_dir,
                   std::string const& out_dir, std::optional<std::string> const& ext, bool force, bool dry_run,
                   bool guess) {
    if (has_out_dir && has_output)
        throw usage_error{std::format("{}: use either -o/--output or --out-dir, not both\n", kProg)};
    std::vector<std::pair<std::filesystem::path, match const*>> targets;
    if (has_output) {
        if (matches.size() != 1) {
            throw usage_error{
                std::format("{}: -o/--output needs exactly one block, got {} (narrow the selection or use "
                            "--out-dir)\n",
                            kProg, matches.size())};
        }
        targets.emplace_back(output, &matches.front());
    } else {
        std::set<std::string> used;
        for (auto const& m : matches) {
            auto name = target_name(m, ext, guess);
            if (used.contains(name)) {
                auto const dot = name.rfind('.');
                auto const stem = dot == std::string::npos ? name : name.substr(0, dot);
                auto const suffix = dot == std::string::npos ? std::string{} : name.substr(dot);
                name = std::format("{}-{}{}", stem, m.index, suffix);
            }
            used.insert(name);
            targets.emplace_back(std::filesystem::path(out_dir) / name, &m);
        }
    }

    std::vector<std::string> conflicts;
    for (auto const& [path, m] : targets) {
        (void)m;
        if (std::filesystem::exists(path) && !force) conflicts.push_back(path.string());
    }
    if (!conflicts.empty()) {
        std::string list;
        for (auto const& conflict : conflicts) {
            if (!list.empty()) list += ", ";
            list += conflict;
        }
        throw usage_error{std::format("{}: refusing to overwrite (use --force): {}\n", kProg, list)};
    }

    if (dry_run) {
        for (auto const& [path, m] : targets) {
            std::cout << path.string() << '\t' << prompt::extract::count_lines(m->source) << " lines\n";
        }
        return;
    }
    for (auto const& [path, m] : targets) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << rstrip_newlines(m->source) << '\n';
        std::fputs(std::format("{}: wrote {}\n", kProg, path.string()).c_str(), stderr);
    }
}

// --- Argument parsing -------------------------------------------------------

struct options {
    std::vector<std::string> files;
    std::vector<std::string> kind;
    std::vector<std::string> lang;
    bool guess = false;
    std::vector<std::string> heading;
    std::vector<std::string> under;
    std::vector<std::string> before;
    std::vector<std::string> after;
    std::vector<std::string> title;
    std::vector<std::string> grep;
    bool has_index = false;
    std::string index;
    bool first = false;
    bool last = false;
    bool one = false;
    bool literal = false;
    bool indented = false;
    bool no_subsections = false;
    bool has_select = false;
    std::string select;
    bool list = false;
    bool pick = false;
    bool count = false;
    bool fence = false;
    bool with_heading = false;
    bool with_intro = false;
    bool json = false;
    bool jsonl = false;
    bool has_items = false;
    std::string items;
    std::vector<std::string> contract;
    bool kinds = false;
    bool no_header = false;
    bool has_output = false;
    std::string output;
    bool has_out_dir = false;
    std::string out_dir;
    bool has_ext = false;
    std::string ext;
    bool force = false;
    bool dry_run = false;
    bool help = false;
};

void append_comma_list(std::vector<std::string>& target, std::string_view value) {
    std::size_t begin = 0;
    while (begin <= value.size()) {
        auto const end = value.find(',', begin);
        auto part = trim(value.substr(begin, (end == std::string_view::npos ? value.size() : end) - begin));
        if (!part.empty()) target.emplace_back(part);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
}

[[noreturn]] void unknown_option(std::string_view token) {
    throw usage_error{std::format("{}: error: unrecognized arguments: {}\n", kProg, token)};
}

std::string need_value(std::span<char* const> argv, std::size_t& i, std::string_view flag,
                       std::string_view inline_value) {
    if (!inline_value.empty()) return std::string(inline_value);
    if (i + 1 >= argv.size()) throw usage_error{std::format("{}: {} needs a value\n", kProg, flag)};
    return {argv[++i]};
}

options parse_options(std::span<char* const> argv) {
    options out;
    bool positional_only = false;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        std::string_view const token(argv[i]);
        if (positional_only || token.empty() || token == "-" || token.front() != '-') {
            out.files.emplace_back(token);
            continue;
        }
        if (token == "--") {
            positional_only = true;
            continue;
        }
        std::string_view flag = token;
        std::string_view inline_value;
        if (auto const equals = token.find('='); equals != std::string_view::npos) {
            flag = token.substr(0, equals);
            inline_value = token.substr(equals + 1);
        }
        if (flag == "-h" || flag == "--help") {
            out.help = true;
        } else if (flag == "--kind") {
            append_comma_list(out.kind, need_value(argv, i, flag, inline_value));
        } else if (flag == "--lang") {
            append_comma_list(out.lang, need_value(argv, i, flag, inline_value));
        } else if (flag == "--guess") {
            out.guess = true;
        } else if (flag == "--heading") {
            append_comma_list(out.heading, need_value(argv, i, flag, inline_value));
        } else if (flag == "--under") {
            append_comma_list(out.under, need_value(argv, i, flag, inline_value));
        } else if (flag == "--before") {
            append_comma_list(out.before, need_value(argv, i, flag, inline_value));
        } else if (flag == "--after") {
            append_comma_list(out.after, need_value(argv, i, flag, inline_value));
        } else if (flag == "--title") {
            append_comma_list(out.title, need_value(argv, i, flag, inline_value));
        } else if (flag == "--grep") {
            append_comma_list(out.grep, need_value(argv, i, flag, inline_value));
        } else if (flag == "-n" || flag == "--index") {
            out.has_index = true;
            out.index = need_value(argv, i, flag, inline_value);
        } else if (flag == "--first") {
            out.first = true;
        } else if (flag == "--last") {
            out.last = true;
        } else if (flag == "--one") {
            out.one = true;
        } else if (flag == "-F" || flag == "--literal") {
            out.literal = true;
        } else if (flag == "--indented") {
            out.indented = true;
        } else if (flag == "--no-subsections") {
            out.no_subsections = true;
        } else if (flag == "--select") {
            out.has_select = true;
            out.select = need_value(argv, i, flag, inline_value);
        } else if (flag == "--list") {
            out.list = true;
        } else if (flag == "--pick") {
            out.pick = true;
        } else if (flag == "-c" || flag == "--count") {
            out.count = true;
        } else if (flag == "--fence") {
            out.fence = true;
        } else if (flag == "--with-heading") {
            out.with_heading = true;
        } else if (flag == "--with-intro") {
            out.with_intro = true;
        } else if (flag == "--json") {
            out.json = true;
        } else if (flag == "--jsonl") {
            out.jsonl = true;
        } else if (flag == "--items") {
            out.has_items = true;
            out.items = need_value(argv, i, flag, inline_value);
        } else if (flag == "--contract") {
            out.contract.push_back(need_value(argv, i, flag, inline_value));
        } else if (flag == "--kinds") {
            out.kinds = true;
        } else if (flag == "--no-header") {
            out.no_header = true;
        } else if (flag == "-o" || flag == "--output") {
            out.has_output = true;
            out.output = need_value(argv, i, flag, inline_value);
        } else if (flag == "--out-dir") {
            out.has_out_dir = true;
            out.out_dir = need_value(argv, i, flag, inline_value);
        } else if (flag == "--ext") {
            out.has_ext = true;
            out.ext = need_value(argv, i, flag, inline_value);
        } else if (flag == "--force") {
            out.force = true;
        } else if (flag == "--dry-run") {
            out.dry_run = true;
        } else {
            unknown_option(token);
        }
    }
    return out;
}

std::string read_file(std::string const& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw usage_error{std::format("{}: cannot read {}: {}\n", kProg, path, std::strerror(errno))};
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return text;
}

std::string read_stdin() { return {(std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>()}; }

void print_help() {
    std::cout << R"MDX(usage: md.extract [-h] [--kind KINDS] [--lang LANGS] [--guess]
                  [--heading PAT] [--under PAT] [--before PAT] [--after PAT]
                  [--title PAT] [--grep PAT] [-n SPEC] [--first] [--last]
                  [--one] [-F] [--indented] [--no-subsections] [--select SPEC]
                  [--list] [--pick] [-c] [--fence] [--with-heading]
                  [--with-intro] [--json] [--jsonl] [--items SPEC]
                  [--contract SPEC] [--kinds] [--no-header] [-o FILE]
                  [--out-dir DIR] [--ext EXT] [--force] [--dry-run]
                  [FILE ...]

Extract blocks from Markdown on stdin or in FILE arguments. `--list` numbers the
rows of the current selection, so `--list` then `-n N` (with the same flags) is a
reliable two-step workflow; every other selector is an independent filter, and
they combine with AND.

Default output is the block body alone, so the tool composes:

  md.extract --lang diff answer.md | git apply
  md.extract --kind csv --before "monthly totals" report.md > totals.csv
  md.extract --select table:symbol answer.md
  md.extract --select command:prompt answer.md | sh
  md.extract --items table:symbol,reason answer.md
  md.extract --contract command:prompt

SELECTORS
  --select SPEC, --items SPEC and --contract SPEC share one grammar:
  kind[:qualifier][/refinement][@N]. See --kinds for the current grammar.

EXIT STATUS
  0  at least one block was printed or written
  1  nothing matched (or the picker was cancelled)
  2  usage error
)MDX";
}

int run(options const& args) {
    if (args.kinds) {
        std::cout << prompt::extract::kinds_help();
        return 0;
    }
    if (!args.contract.empty()) {
        std::vector<selection> selectors;
        selectors.reserve(args.contract.size());
        for (auto const& spec : args.contract) selectors.push_back(prompt::extract::parse_selection(spec));
        if (selectors.size() == 1) std::cout << prompt::extract::contract_for(selectors.front());
        else
            std::cout << prompt::extract::combined_contract(selectors);
        return 0;
    }

    std::optional<selection> const selector =
        args.has_select ? std::optional<selection>(prompt::extract::parse_selection(args.select)) : std::nullopt;

    std::set<std::string> kinds =
        args.kind.empty() ? std::set<std::string>{"code"} : std::set<std::string>(args.kind.begin(), args.kind.end());
    if (kinds.contains("any") || kinds.contains("all")) {
        kinds = {"code", "table", "section", "frontmatter"};
    }
    if (kinds.contains("field") && !selector.has_value()) {
        throw usage_error{std::format("{}: the field kind needs a label: use --select field:LABEL\n", kProg)};
    }
    static std::set<std::string> const known = {"code", "table", "section", "frontmatter", "command", "field"};
    for (auto const& kind : kinds) {
        if (!known.contains(kind)) {
            throw usage_error{std::format(
                "{}: unknown kind {} (use code, table, section, frontmatter, command, field or any)\n", kProg, kind)};
        }
    }

    std::optional<std::vector<int>> index_values;
    if (args.has_index) index_values = parse_index_spec(args.index);
    if (args.first && args.last)
        throw usage_error{std::format("{}: --first and --last are mutually exclusive\n", kProg)};
    if ((args.first || args.last) && index_values.has_value()) {
        throw usage_error{std::format("{}: --first/--last cannot be combined with --index\n", kProg)};
    }
    if (args.first) index_values = std::vector<int>{1};
    if (args.last) index_values = std::vector<int>{-1};
    if (selector.has_value() && index_values.has_value()) {
        throw usage_error{std::format("{}: --select has its own @N position; drop --index/--first/--last\n", kProg)};
    }
    if (args.dry_run && !args.has_output && !args.has_out_dir) {
        throw usage_error{std::format("{}: --dry-run needs -o/--output or --out-dir\n", kProg)};
    }
    if (args.json && args.jsonl) throw usage_error{std::format("{}: use either --json or --jsonl\n", kProg)};

    query const q{
        .kinds = kinds,
        .langs = args.lang,
        .guess = args.guess,
        .heading = compile_patterns(args.heading, args.literal),
        .under = compile_patterns(args.under, args.literal),
        .before = compile_patterns(args.before, args.literal),
        .after = compile_patterns(args.after, args.literal),
        .title = compile_patterns(args.title, args.literal),
        .grep = compile_patterns(args.grep, args.literal),
    };

    std::vector<std::string> sources = args.files;
    if (sources.empty()) {
        if (isatty(STDIN_FILENO) != 0) {
            throw usage_error{std::format("{}: no input: pass FILE arguments or pipe Markdown on stdin\n", kProg)};
        }
        sources.emplace_back("-");
    }

    std::vector<block> blocks;
    std::vector<std::pair<std::string, std::string>> texts;
    int file_order = 0;
    for (auto const& source : sources) {
        auto text = source == "-" ? read_stdin() : read_file(source);
        texts.emplace_back(source, text);
        prompt::extract::parse_options const parse_options{
            .indented = args.indented,
            .include_subsections = !args.no_subsections,
        };
        auto parsed = prompt::extract::parse_document(text, source, file_order, parse_options);
        for (auto& b : parsed) blocks.push_back(std::move(b));
        ++file_order;
    }
    {
        std::vector<block> commands;
        for (auto const& b : blocks) {
            if (auto command = prompt::extract::derive_command(b); command.has_value())
                commands.push_back(std::move(*command));
        }
        for (auto& command : commands) blocks.push_back(std::move(command));
    }
    prompt::extract::assign_indices(blocks);

    if (args.has_items) {
        auto const items_selector = prompt::extract::parse_selection(args.items);
        // The engine parses each source itself.
        std::vector<match> items_matches;
        for (auto const& [name, text] : texts) {
            (void)name;
            auto part = prompt::extract::run(text, items_selector);
            for (auto& m : part) items_matches.push_back(std::move(m));
        }
        auto const items = prompt::extract::items_to_json(items_matches, args.guess);
        if (items.empty()) throw soft_error{std::format("{}: selector '{}' produced no items\n", kProg, args.items)};
        std::cout << items.dump(2) << '\n';
        return 0;
    }

    std::vector<match> selected;
    if (selector.has_value()) {
        for (auto const& [name, text] : texts) {
            (void)name;
            auto part = prompt::extract::run(text, *selector);
            for (auto& m : part) selected.push_back(std::move(m));
        }
    } else {
        selected = as_matches(select_blocks(blocks, q, index_values));
    }
    for (std::size_t position = 0; position < selected.size(); ++position) selected[position].index = position + 1;

    if (args.one && selected.size() != 1) {
        if (selected.empty()) throw soft_error{std::format("{}: no block matches --one\n", kProg)};
        throw usage_error{
            std::format("{}: --one needs exactly one block, got {} (narrow the selection or drop --one)\n", kProg,
                        selected.size())};
    }
    if (args.count) {
        std::cout << selected.size() << '\n';
        return selected.empty() ? 1 : 0;
    }
    if (selected.empty()) {
        auto const names = selector.has_value() ? prompt::extract::selector_kind_names(*selector) : join_sorted(kinds);
        throw soft_error{std::format("{}: no matching {} blocks (try --list, --kind any, or --guess)\n", kProg, names)};
    }
    if (args.list) {
        std::cout << prompt::extract::render_list(selected, args.guess);
        return 0;
    }
    if (args.pick) selected = prompt::extract::pick(selected, args.guess);

    if (args.json) {
        auto array = nlohmann::ordered_json::array();
        for (auto const& m : selected) array.push_back(prompt::extract::block_record(m, args.guess));
        std::cout << array.dump(2) << '\n';
        return 0;
    }
    if (args.jsonl) {
        for (auto const& m : selected) std::cout << spaced_json(prompt::extract::block_record(m, args.guess)) << '\n';
        return 0;
    }

    auto const options = render_options_from(args.with_heading, args.with_intro, args.fence);
    if (selector.has_value()) {
        if (args.has_out_dir) throw usage_error{std::format("{}: --select cannot be combined with --out-dir\n", kProg)};
        auto const rendered = prompt::extract::render_selection(selected, *selector, args.no_header, options);
        if (args.has_output && args.output != "-") {
            std::ofstream file(args.output);
            file << rendered;
        } else {
            std::cout << rendered;
        }
        return 0;
    }
    if (args.output == "-") {
        std::cout << prompt::extract::render_many(selected, options);
        return 0;
    }
    if (args.has_output || args.has_out_dir) {
        write_matches(selected, args.has_output, args.output, args.has_out_dir, args.out_dir,
                      args.has_ext ? std::optional<std::string>(args.ext) : std::nullopt, args.force, args.dry_run,
                      args.guess);
        return 0;
    }
    std::cout << prompt::extract::render_many(selected, options);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // Engine messages are unprefixed; CLI messages already carry `md.extract: `.
    auto report = [](char const* what) {
        std::string_view const message(what);
        if (message.starts_with(std::string(kProg) + ": ")) {
            std::fputs(what, stderr);
        } else {
            std::fprintf(stderr, "%.*s: %s", static_cast<int>(kProg.size()), kProg.data(), what);
        }
        if (message.empty() || message.back() != '\n') std::fputc('\n', stderr);
    };
    try {
        std::vector<char*> tokens;
        for (int i = 1; i < argc; ++i) tokens.push_back(argv[i]);
        auto const args = parse_options(tokens);
        if (args.help) {
            print_help();
            return 0;
        }
        return run(args);
    } catch (soft_error const& error) {
        report(error.what());
        return 1;
    } catch (usage_error const& error) {
        report(error.what());
        return 2;
    } catch (std::exception const& error) {
        report(error.what());
        return 2;
    }
}
