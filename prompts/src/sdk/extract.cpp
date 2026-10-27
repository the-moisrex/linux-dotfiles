#include "extract.hpp"
#include "facts.hpp"
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
#include <vector>

namespace prompt::extract {

namespace {

std::array<extractor, extractor_capacity> g_units{};
std::size_t g_count = 0;

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return text;
}

std::string to_lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char const c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

bool is_integer(std::string_view text) {
    if (text.empty()) return false;
    std::size_t i = (text.front() == '-') ? 1 : 0;
    if (i >= text.size()) return false;
    return std::ranges::all_of(text.substr(i), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

// A binding name must look like a fact key; anything else before `=` stays part
// of the selector (so `section:a=b` is a heading, not a binding).
bool valid_fact_key(std::string_view name) {
    if (name.empty()) return false;
    auto const first = static_cast<unsigned char>(name.front());
    if (std::isalpha(first) == 0 && name.front() != '_') return false;
    return std::ranges::all_of(name, [](char c) {
        auto const uc = static_cast<unsigned char>(c);
        return std::isalnum(uc) != 0 || c == '_' || c == '.' || c == '-';
    });
}

// The name at the head of a stage (up to `:`, `@` or `/`).
std::string_view stage_name(std::string_view text) {
    std::size_t end = 0;
    while (end < text.size() && text[end] != ':' && text[end] != '@' && text[end] != '/') ++end;
    return text.substr(0, end);
}

// Does `rest` begin with a stage (a registered extractor name)?
bool starts_stage(std::string_view rest) {
    auto const name = stage_name(rest);
    if (name.empty()) return false;
    return is_extractor_name(to_lower(name));
}

std::string unescape_slash(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '/') {
            out.push_back('/');
            ++i;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::string escape_slash(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char const c : text) {
        if (c == '/') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

step parse_step(std::string_view segment) {
    segment = trim(segment);
    if (segment.empty()) throw usage_error{"prompt: empty selector stage\n"};

    step out;
    auto const at = segment.rfind('@');
    if (at != std::string_view::npos && is_integer(segment.substr(at + 1))) {
        out.position = std::stoi(std::string(segment.substr(at + 1)));
        segment = trim(segment.substr(0, at));
    }

    auto const colon = segment.find(':');
    if (colon == std::string_view::npos) {
        out.name = to_lower(segment);
    } else {
        out.name = to_lower(trim(segment.substr(0, colon)));
        out.qualifier = unescape_slash(trim(segment.substr(colon + 1)));
    }

    if (!is_extractor_name(out.name)) {
        // A bare word is the heading of the section to take.
        out.name = "section";
        out.qualifier = unescape_slash(segment);
        return out;
    }
    if (out.name == "all") out.name = "any";
    return out;
}

std::string format_step(step const& s) {
    std::string out = s.name;
    if (!s.qualifier.empty()) {
        out += ':';
        out += escape_slash(s.qualifier);
    }
    if (s.position != 0) out += std::format("@{}", s.position);
    return out;
}

std::vector<match> apply_position(std::vector<match> matches, int position) {
    if (position == 0) return matches;
    auto const size = static_cast<long>(matches.size());
    long const index = position > 0 ? position - 1 : size + position;
    if (index < 0 || index >= size) {
        throw soft_error{std::format("position {} is out of range (the selector matched {})\n", position,
                                     size == 1 ? "1 block" : std::format("{} blocks", size))};
    }
    std::vector<match> picked;
    picked.push_back(std::move(matches[static_cast<std::size_t>(index)]));
    return picked;
}

// Every registered extractor, name-sorted (registration order across
// translation units is unspecified).
std::vector<extractor const*> sorted_extractors() {
    std::vector<extractor const*> out;
    for (auto const& unit : get_all_extractors()) out.push_back(&unit);
    std::ranges::sort(out, {}, [](extractor const* unit) { return unit->name; });
    return out;
}

std::string clause_of(selection const& sel) {
    if (sel.steps.empty()) return "the requested result";
    auto const* unit = find_extractor(sel.steps.front().name);
    if (unit != nullptr && unit->contract != nullptr) {
        auto text = unit->contract(sel.steps.front().qualifier);
        if (!text.clause.empty()) return text.clause;
    }
    return "the requested result";
}

} // namespace

bool register_extractor(extractor const& desc) noexcept {
    if (g_count >= g_units.size()) {
        std::fputs("prompt: extractor registry is full; raise extractor_capacity\n", stderr);
        std::abort();
    }
    g_units[g_count++] = desc;
    return true;
}

std::span<extractor const> get_all_extractors() { return {g_units.data(), g_count}; }

extractor const* find_extractor(std::string_view name) {
    for (auto const& unit : get_all_extractors()) {
        if (unit.name == name) return &unit;
    }
    return nullptr;
}

bool is_extractor_name(std::string_view name) {
    static constexpr std::array<std::string_view, 4> keywords = {"auto", "any", "all", "none"};
    if (std::ranges::find(keywords, name) != keywords.end()) return true;
    return find_extractor(name) != nullptr;
}

selection parse_selection(std::string_view raw) {
    raw = trim(raw);
    if (raw.empty()) throw usage_error{"prompt: empty selector\n"};

    selection out;
    if (auto const equals = raw.find('='); equals != std::string_view::npos) {
        auto const name = trim(raw.substr(0, equals));
        if (valid_fact_key(name)) {
            out.bind = std::string(name);
            raw = trim(raw.substr(equals + 1));
        }
    }
    if (raw.empty()) throw usage_error{"prompt: empty selector after the binding\n"};

    // Split on `/` only before a registered stage, so a heading or a JSON path
    // may contain a slash. `\/` escapes a literal separator.
    std::size_t start = 0;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            ++i;
            continue;
        }
        if (raw[i] != '/') continue;
        if (starts_stage(raw.substr(i + 1))) {
            out.steps.push_back(parse_step(raw.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.steps.push_back(parse_step(raw.substr(start)));
    return out;
}

std::vector<selection> parse_plan(std::string_view plan) {
    std::vector<selection> entries;
    std::size_t begin = 0;
    while (begin <= plan.size()) {
        auto const end = plan.find('\n', begin);
        auto const line = trim(plan.substr(begin, (end == std::string_view::npos ? plan.size() : end) - begin));
        if (!line.empty()) entries.push_back(parse_selection(line));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return entries;
}

std::string format_plan(std::span<selection const> entries) {
    std::string out;
    for (auto const& entry : entries) {
        if (!out.empty()) out += '\n';
        if (!entry.bind.empty()) {
            out += entry.bind;
            out += '=';
        }
        for (std::size_t i = 0; i < entry.steps.size(); ++i) {
            if (i != 0) out += '/';
            out += format_step(entry.steps[i]);
        }
    }
    return out;
}

std::optional<selection> primary(std::string_view plan) {
    auto const entries = parse_plan(plan);
    for (auto const& entry : entries) {
        if (entry.bind.empty()) return entry;
    }
    if (entries.size() == 1) return entries.front();
    if (entries.empty()) return std::nullopt;
    throw usage_error{"prompt: the expect plan has no primary selector (an unnamed `[name=]selector` entry)\n"};
}

bool is_selector_token(std::string_view token) {
    if (token.empty()) return false;
    if (auto const at = token.rfind('@'); at != std::string_view::npos && is_integer(token.substr(at + 1))) {
        token = token.substr(0, at);
    }
    if (is_extractor_name(to_lower(token))) return true;
    // The first stage, or any stage of a pipeline.
    std::size_t start = 0;
    while (start <= token.size()) {
        auto const next = token.find('/', start);
        auto const segment = token.substr(start, (next == std::string_view::npos ? token.size() : next) - start);
        if (is_extractor_name(to_lower(stage_name(segment)))) return true;
        if (next == std::string_view::npos) break;
        start = next + 1;
    }
    return false;
}

std::vector<match> run(std::string_view document, selection const& sel) {
    std::vector<match> current;
    match seed;
    seed.text = std::string(document);
    seed.source = std::string(document);
    seed.kind = "document";
    current.push_back(std::move(seed));
    for (auto const& stage : sel.steps) {
        auto const* unit = find_extractor(stage.name);
        if (unit == nullptr) throw usage_error{std::format("prompt: unknown extractor {}\n", stage.name)};
        std::vector<match> next;
        for (auto const& previous : current) {
            auto part = unit->run(previous.source, stage.qualifier);
            for (auto& one : part) next.push_back(std::move(one));
        }
        current = apply_position(std::move(next), stage.position);
    }
    return current;
}

std::string contract_for(selection const& sel) {
    if (sel.steps.empty()) return {};
    auto const* unit = find_extractor(sel.steps.front().name);
    if (unit != nullptr && unit->contract != nullptr) {
        auto text = unit->contract(sel.steps.front().qualifier);
        if (!text.full.empty()) return text.full;
    }
    return "Return only the requested result (a table, code block, section or command), with no surrounding "
           "commentary.\n";
}

std::string combined_contract(std::span<selection const> entries) {
    std::string out = "Return one answer containing all of the following, and nothing else:\n";
    bool has_field = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        out += std::format("{}. {}.\n", i + 1, clause_of(entries[i]));
        if (!entries[i].steps.empty() && entries[i].steps.front().name == "field") has_field = true;
    }
    if (has_field) {
        out += "Put every `name: <value>` line in one block so it is easy to read, and use the exact label shown.\n";
    }
    return out;
}

std::string kinds_help() {
    std::string out;
    for (auto const* unit : sorted_extractors()) {
        out += std::format("{:<12} {}\n", unit->name, unit->summary);
    }
    return out;
}

std::string selector_kind_names(selection const& sel) {
    if (sel.steps.empty()) return {};
    auto const& name = sel.steps.front().name;
    if (name == "auto" || name == "any" || name == "all") return "code, frontmatter, section, table";
    return name;
}

void complete_selector(completion_request const& request, std::ostream& os) {
    std::string_view const prefix = request.prefix;
    auto const slash = prefix.rfind('/');
    std::string_view const base = slash == std::string_view::npos ? prefix : prefix.substr(slash + 1);

    if (auto const colon = base.find(':'); colon != std::string_view::npos) {
        auto const* unit = find_extractor(to_lower(base.substr(0, colon)));
        if (unit != nullptr && unit->complete != nullptr) {
            unit->complete(base.substr(colon + 1), os);
            return;
        }
    }

    auto const strip = slash == std::string_view::npos ? prefix : base;
    for (auto const* unit : sorted_extractors()) {
        if (completion::matches(strip, unit->name)) completion::write_item(os, unit->name, unit->summary);
        std::string const with_colon = std::string(unit->name) + ":";
        if (completion::matches(strip, with_colon)) completion::write_item(os, with_colon, unit->summary);
    }
    for (char const* keyword : {"auto", "any", "none"}) {
        if (completion::matches(strip, keyword)) completion::write_item(os, keyword, "let the extractor choose");
    }
    std::string const binding = std::string(keys::save_stem) + "=";
    if (completion::matches(strip, binding)) completion::write_item(os, binding, "publish the value as a fact");
}

} // namespace prompt::extract
