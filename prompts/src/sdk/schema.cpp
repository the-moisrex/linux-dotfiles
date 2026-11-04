#include "schema.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::schema {

namespace {

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// Strip the leading dashes and a trailing comma from one flag spelling.
std::string_view flag_token(std::string_view word) {
    if (word.ends_with(',')) word.remove_suffix(1);
    while (!word.empty() && word.front() == '-') word.remove_prefix(1);
    return word;
}

bool is_integer_name(std::string_view name) {
    static constexpr std::string_view kInt = "N M SIZE COUNT LINES DEPTH INDEX";
    std::string upper;
    upper.reserve(name.size());
    for (char const ch : name) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    return kInt.contains(upper) || upper == "N";
}

// A value placeholder is a token like `N`, `MODE` or `<file>` that follows the
// flag spellings; anything dash-led is another flag.
bool looks_like_placeholder(std::string_view word) {
    if (word.empty() || word.front() == '-') return false;
    if (word.front() == '<' || word.front() == '[') return true;
    for (char const ch : word) {
        if (std::isupper(static_cast<unsigned char>(ch)) != 0) return true;
    }
    return word.size() <= 4; // N, M, DIR, FMT, ...
}

// `-a, --all VALUE` or `--head N`, the flag column both renderers share.
std::string flag_label(parameter const& par, bool with_placeholder) {
    std::string flag =
        par.alias.empty() ? std::format("--{}", par.name) : std::format("-{}, --{}", par.alias, par.name);
    if (with_placeholder && par.takes_value) {
        flag += ' ';
        flag += par.value_placeholder.empty() ? "VALUE" : par.value_placeholder;
    }
    return flag;
}

} // namespace

std::string_view to_string(type value) {
    switch (value) {
    case type::boolean:
        return "boolean";
    case type::string:
        return "string";
    case type::integer:
        return "integer";
    }
    return "string";
}

parameter bool_flag(std::string name, std::string description, std::string alias) {
    parameter par;
    par.name = std::move(name);
    par.description = std::move(description);
    par.kind = type::boolean;
    par.alias = std::move(alias);
    return par;
}

parameter str_flag(std::string name, std::string description, std::string placeholder, std::string alias) {
    parameter par;
    par.name = std::move(name);
    par.description = std::move(description);
    par.kind = type::string;
    par.takes_value = true;
    par.value_placeholder = std::move(placeholder);
    par.alias = std::move(alias);
    return par;
}

parameter int_flag(std::string name, std::string description, std::string alias) {
    parameter par;
    par.name = std::move(name);
    par.description = std::move(description);
    par.kind = type::integer;
    par.takes_value = true;
    par.value_placeholder = "N";
    par.alias = std::move(alias);
    return par;
}

parameter enum_flag(std::string name, std::string description, std::vector<std::string> values, std::string alias) {
    parameter par;
    par.name = std::move(name);
    par.description = std::move(description);
    par.kind = type::string;
    par.takes_value = true;
    par.value_placeholder = "MODE";
    par.enum_values = std::move(values);
    par.alias = std::move(alias);
    return par;
}

parameter multi_flag(std::string name, std::string description, std::string placeholder, std::string alias) {
    parameter par = str_flag(std::move(name), std::move(description), std::move(placeholder), std::move(alias));
    par.repeatable = true;
    return par;
}

operand argument(std::string name, std::string description, bool required, bool variadic) {
    operand op;
    op.name = std::move(name);
    op.description = std::move(description);
    op.required = required;
    op.variadic = variadic;
    return op;
}

tool make_tool(std::string name, std::string summary, std::vector<parameter> parameters,
               std::vector<operand> operands) {
    tool value;
    value.name = std::move(name);
    value.summary = std::move(summary);
    value.parameters = std::move(parameters);
    value.operands = std::move(operands);
    return value;
}

tool from_help(std::string_view name, std::string_view summary, std::string_view help) {
    tool value;
    value.name = std::string(name);
    value.summary = std::string(summary);

    auto const options = help.find("Options:");
    if (options == std::string_view::npos) return value;
    std::string_view body = help.substr(options + std::string_view{"Options:"}.size());

    std::size_t pos = 0;
    while (pos < body.size()) {
        auto const eol = body.find('\n', pos);
        std::string_view line = eol == std::string_view::npos ? body.substr(pos) : body.substr(pos, eol - pos);
        pos = eol == std::string_view::npos ? body.size() : eol + 1;
        line = trimmed(line);
        if (line.empty() || line.front() != '-') continue; // blank, notes or a continuation line

        // The flag column ends at the first run of two spaces or a tab; the
        // rest is the description.
        std::size_t split = std::string_view::npos;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\t' || (line[i] == ' ' && i + 1 < line.size() && line[i + 1] == ' ')) {
                split = i;
                break;
            }
        }
        std::string_view const left = split == std::string_view::npos ? line : line.substr(0, split);
        std::string_view const desc =
            split == std::string_view::npos ? std::string_view{} : trimmed(line.substr(split));

        std::vector<std::string> longs;
        std::string short_flag;
        std::string placeholder;
        std::size_t p = 0;
        while (p < left.size()) {
            while (p < left.size() && (left[p] == ' ' || left[p] == '\t')) ++p;
            if (p >= left.size()) break;
            auto const end = left.find_first_of(" \t", p);
            std::string_view const word =
                left.substr(p, end == std::string_view::npos ? std::string_view::npos : end - p);
            p = end == std::string_view::npos ? left.size() : end;
            if (word.starts_with("--")) {
                longs.emplace_back(flag_token(word));
            } else if (word.starts_with('-') && word.size() >= 2) {
                // A single dash is a short spelling, even with a trailing comma
                // (`-i, --ignore-case`).
                short_flag = std::string(flag_token(word));
            } else if (placeholder.empty() && looks_like_placeholder(word)) {
                placeholder = std::string(word);
            }
        }
        if (longs.empty() && short_flag.empty()) continue;

        bool const takes_value = !placeholder.empty();
        for (std::size_t i = 0; i < longs.size(); ++i) {
            parameter par;
            par.name = longs[i];
            par.description = std::string(desc);
            par.takes_value = takes_value;
            par.value_placeholder = placeholder;
            if (takes_value) par.kind = is_integer_name(placeholder) ? type::integer : type::string;
            if (i == 0 && !short_flag.empty()) par.alias = short_flag;
            value.parameters.push_back(std::move(par));
        }
        if (longs.empty() && !short_flag.empty()) {
            parameter par;
            par.name = short_flag;
            par.alias = short_flag;
            par.description = std::string(desc);
            par.takes_value = takes_value;
            par.value_placeholder = placeholder;
            if (takes_value) par.kind = is_integer_name(placeholder) ? type::integer : type::string;
            value.parameters.push_back(std::move(par));
        }
    }
    return value;
}

std::string to_markdown(tool const& value) {
    std::string out = std::format("### {}\n\n", value.name);
    out += value.summary;
    out += '\n';
    if (!value.operands.empty()) {
        out += "\nOperands:\n";
        for (auto const& op : value.operands) {
            out += std::format("  {}{}{} — {}\n", op.name, op.variadic ? "..." : "", op.required ? " (required)" : "",
                               op.description);
        }
    }
    if (!value.parameters.empty()) {
        out += "\nOptions:\n";
        for (auto const& par : value.parameters) {
            std::string const flag = flag_label(par, true);
            out += std::format("  {}{}{}\n", flag, par.repeatable ? " (repeatable)" : "",
                               par.description.empty() ? "" : std::format(" — {}", par.description));
        }
    }
    return out;
}

std::string render_options(tool const& value, std::string_view extra) {
    std::string out = "Options:\n";
    for (auto const& par : value.parameters) {
        out += std::format("  {:<18} {}\n", flag_label(par, true), par.description);
    }
    if (!extra.empty()) {
        out += extra;
        if (!out.ends_with('\n')) out += '\n';
    }
    return out;
}

void complete(tool const& value, completion_request const& req, std::ostream& os) {
    for (auto const& par : value.parameters) {
        if (!par.enum_values.empty()) {
            std::vector<std::string_view> values;
            values.reserve(par.enum_values.size());
            for (auto const& choice : par.enum_values) values.push_back(choice);
            if (completion::write_values_after_flag(os, req, "--" + par.name, values)) return;
        }
        std::string long_flag = "--" + par.name;
        if (completion::matches(req.prefix, long_flag)) completion::write_item(os, long_flag, par.description);
        if (!par.alias.empty()) {
            std::string const short_flag = "-" + par.alias;
            if (completion::matches(req.prefix, short_flag)) completion::write_item(os, short_flag, par.description);
        }
    }
}

} // namespace prompt::schema
