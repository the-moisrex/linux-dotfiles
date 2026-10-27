#include "../sdk/errors.hpp"
#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::optional<nlohmann::json const*> walk(nlohmann::json const& root, std::string_view path) {
    nlohmann::json const* current = &root;
    std::size_t i = 0;
    if (!path.empty() && path.front() == '$') ++i;
    if (i < path.size() && path[i] == '/') {
        try {
            current = &current->at(nlohmann::json::json_pointer(std::string(path)));
        } catch (nlohmann::json::exception const&) {
            return std::nullopt;
        }
        return current;
    }
    while (i < path.size()) {
        if (path[i] == '.') {
            ++i;
            if (i >= path.size()) break;
        }
        if (path[i] == '[') {
            ++i;
            auto const close = path.find(']', i);
            if (close == std::string_view::npos) return std::nullopt;
            auto token = std::string(trim_view(path.substr(i, close - i)));
            i = close + 1;
            if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
                token = token.substr(1, token.size() - 2);
            bool const digits = !token.empty() && std::ranges::all_of(token, [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) != 0;
            });
            try {
                if (digits) current = &current->at(static_cast<std::size_t>(std::stoul(token)));
                else
                    current = &current->at(token);
            } catch (nlohmann::json::exception const&) {
                return std::nullopt;
            }
            continue;
        }
        auto const begin = i;
        while (i < path.size() && path[i] != '.' && path[i] != '[') ++i;
        auto const key = trim_view(path.substr(begin, i - begin));
        if (key.empty()) continue;
        if (!current->is_object() || !current->contains(key)) return std::nullopt;
        current = &current->at(key);
    }
    return current;
}

std::string render_value(nlohmann::json const& value) {
    if (value.is_string()) return value.get<std::string>();
    return value.dump(2);
}

std::vector<match> extract_from(nlohmann::json const& root, std::string_view path, std::size_t line) {
    auto const found = walk(root, path);
    if (!found.has_value()) return {};
    match m;
    m.kind = "json";
    m.lang = "json";
    m.title = std::string(path);
    m.text = render_value(**found);
    m.source = m.text;
    m.line = line;
    m.end_line = line;
    return {std::move(m)};
}

std::vector<match> run_json(std::string_view text, std::string_view qualifier) {
    auto const path = trim_view(qualifier);
    if (auto parsed = nlohmann::json::parse(text, nullptr, false); !parsed.is_discarded()) {
        auto out = extract_from(parsed, path, 0);
        if (out.empty()) throw soft_error{std::format("no JSON value at `{}`\n", path)};
        return out;
    }
    auto blocks = parse_document(text, "");
    std::vector<match> out;
    for (auto const& b : blocks) {
        if (b.kind != "code") continue;
        if (!b.lang.empty() && b.lang != "json") continue;
        auto parsed = nlohmann::json::parse(b.body, nullptr, false);
        if (parsed.is_discarded()) continue;
        for (auto& m : extract_from(parsed, path, b.start_line)) out.push_back(std::move(m));
    }
    if (out.empty()) throw soft_error{std::format("no JSON value at `{}`\n", path)};
    return out;
}

contract_text contract_json(std::string_view qualifier) {
    (void)qualifier;
    return make_contract("Return exactly one fenced `json` code block containing the data. "
                         "Put no prose before or after the block.\n",
                         "one fenced `json` code block containing the data");
}

} // namespace

bool const registered_json = register_extractor(extractor{
    .name = "json",
    .summary = "a JSON value at a path; json:$.a.b reads it from the block or the document",
    .run = &run_json,
    .contract = &contract_json,
});

} // namespace prompt::extract
