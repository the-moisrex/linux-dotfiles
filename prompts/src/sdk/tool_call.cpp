#include "tool_call.hpp"
#include "../extract/document.hpp"
#include "errors.hpp"
#include "prompt_registry.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::schema {

namespace {

std::string scalar_to_string(nlohmann::json const& value, std::string_view key, std::string_view kind) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    throw usage_error{std::format("tool input '{}': expected {}, got {}\n", key, kind, value.type_name())};
}

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

// Operands are documented in upper case (`PATTERN`) but exposed to a model in
// lower case (`pattern`), the spelling every tool schema uses.
std::string operand_key(operand const& op) { return lowercase(op.name); }

bool is_known_key(tool const& value, std::string_view key) {
    return std::ranges::any_of(value.parameters, [&](parameter const& par) { return par.name == key; }) ||
           std::ranges::any_of(value.operands, [&](operand const& op) { return operand_key(op) == key; });
}

} // namespace

nlohmann::ordered_json to_json_value(tool const& value) {
    using nlohmann::ordered_json;
    ordered_json out;
    out["name"] = value.name;
    out["description"] = value.summary;

    ordered_json properties = ordered_json::object();
    ordered_json required = ordered_json::array();
    for (auto const& par : value.parameters) {
        ordered_json property;
        if (par.repeatable) {
            property["type"] = "array";
            property["items"] = ordered_json{{"type", to_string(par.kind)}};
        } else {
            property["type"] = to_string(par.kind);
        }
        if (!par.description.empty()) property["description"] = par.description;
        if (!par.enum_values.empty()) property["enum"] = par.enum_values;
        property["x-flag"] = "--" + par.name;
        properties[par.name] = std::move(property);
        if (par.required) required.push_back(par.name);
    }
    for (auto const& op : value.operands) {
        ordered_json property;
        // A variadic operand takes a list; advertise that so a model does not
        // send a bare string for a tool that accepts several files/commands.
        if (op.variadic) {
            property["type"] = "array";
            property["items"] = ordered_json{{"type", "string"}};
        } else {
            property["type"] = "string";
        }
        if (!op.description.empty()) property["description"] = op.description;
        property["x-positional"] = true;
        std::string const key = operand_key(op);
        properties[key] = std::move(property);
        if (op.required) required.push_back(key);
    }
    out["input"] = ordered_json{{"type", "object"}, {"properties", std::move(properties)}, {"required", required}};
    return out;
}

std::string to_json(tool const& value) { return to_json_value(value).dump(2); }

std::vector<std::string> parse(tool const& value, nlohmann::json const& input) {
    if (!input.is_object()) throw usage_error{"tool input must be a JSON object\n"};

    std::vector<std::string> argv;

    for (auto const& par : value.parameters) {
        auto const it = input.find(par.name);
        if (it == input.end()) {
            if (par.required) throw usage_error{std::format("tool input: missing required '{}'\n", par.name)};
            continue;
        }

        if (par.kind == type::boolean) {
            if (!it->is_boolean()) {
                throw usage_error{
                    std::format("tool input '{}': expected boolean, got {}\n", par.name, it->type_name())};
            }
            if (it->get<bool>()) argv.push_back("--" + par.name);
            continue;
        }

        auto push_one = [&](nlohmann::json const& item) {
            if (par.kind == type::integer && !item.is_number_integer() && !item.is_number_unsigned()) {
                throw usage_error{
                    std::format("tool input '{}': expected integer, got {}\n", par.name, item.type_name())};
            }
            if (par.kind == type::string && !item.is_string()) {
                throw usage_error{
                    std::format("tool input '{}': expected string, got {}\n", par.name, item.type_name())};
            }
            std::string scalar = scalar_to_string(item, par.name, to_string(par.kind));
            if (!par.enum_values.empty() && std::ranges::find(par.enum_values, scalar) == par.enum_values.end()) {
                throw usage_error{
                    std::format("tool input '{}': '{}' is not one of the allowed values\n", par.name, scalar)};
            }
            argv.push_back("--" + par.name);
            argv.push_back(std::move(scalar));
        };

        if (par.repeatable) {
            // A model often sends a lone string for a list-valued flag; treat it
            // as one occurrence instead of rejecting the call.
            if (it->is_array()) {
                for (auto const& item : *it) push_one(item);
            } else {
                push_one(*it);
            }
        } else {
            push_one(*it);
        }
    }

    for (auto const& op : value.operands) {
        std::string const key = operand_key(op);
        auto const it = input.find(key);
        if (it == input.end()) {
            if (op.required) throw usage_error{std::format("tool input: missing required '{}'\n", key)};
            continue;
        }
        if (op.variadic) {
            // Accept both the advertised array and a lone value, so one file or
            // one command does not need to be wrapped by the model.
            if (it->is_array()) {
                for (auto const& item : *it) argv.push_back(scalar_to_string(item, key, "string"));
            } else {
                argv.push_back(scalar_to_string(*it, key, "string"));
            }
        } else if (it->is_array()) {
            if (it->size() != 1) {
                throw usage_error{
                    std::format("tool input '{}': expected a single value, got {} values\n", key, it->size())};
            }
            argv.push_back(scalar_to_string((*it)[0], key, "string"));
        } else {
            argv.push_back(scalar_to_string(*it, key, "string"));
        }
    }

    for (auto const& [key, unused] : input.items()) {
        (void)unused;
        if (!is_known_key(value, key)) throw usage_error{std::format("tool input: unknown parameter '{}'\n", key)};
    }

    return argv;
}

} // namespace prompt::schema

namespace prompt::tool_call {

namespace {

// The call's parameters. Models disagree on the envelope: the documented form
// nests them under `input`, some use the OpenAI-style `arguments`/`parameters`,
// and some put them beside `tool` directly. Accept all three, and a JSON string
// holding the object (a provider that double-encodes).
nlohmann::json input_of(nlohmann::json const& value) {
    for (char const* key : {"input", "arguments", "parameters"}) {
        auto const it = value.find(key);
        if (it == value.end() || it->is_null()) continue;
        if (it->is_object()) return *it;
        if (it->is_string()) {
            if (auto parsed = nlohmann::json::parse(it->get<std::string>(), nullptr, false); parsed.is_object()) {
                return parsed;
            }
        }
        return *it;
    }

    // Flat shape: everything except the call envelope is the input.
    nlohmann::json input = nlohmann::json::object();
    for (auto const& [key, item] : value.items()) {
        if (key == "tool" || key == "id" || key == "type") continue;
        input[key] = item;
    }
    return input;
}

// A top-level array may nest further arrays, so this recurses; the depth is the
// JSON document's own, which nlohmann already bounded when it parsed.
// NOLINTNEXTLINE(misc-no-recursion)
void append_call(std::vector<call>& calls, nlohmann::json const& value, std::string_view block) {
    if (value.is_object()) {
        auto const tool = value.find("tool");
        if (tool == value.end() || !tool->is_string()) return;
        call found;
        found.name = tool->get<std::string>();
        found.input = input_of(value);
        found.block = std::string(block);
        calls.push_back(std::move(found));
    } else if (value.is_array()) {
        for (auto const& item : value) append_call(calls, item, block);
    }
}

} // namespace

std::vector<call> extract(std::string_view text) {
    std::vector<call> calls;

    // A bare JSON document (the whole answer is one call or an array of them).
    if (auto parsed = nlohmann::json::parse(text, nullptr, false); !parsed.is_discarded()) {
        append_call(calls, parsed, text);
        if (!calls.empty()) return calls;
    }

    // Otherwise every fenced block: `json`-tagged or untagged. A block that is
    // valid JSON without a `tool` key contributes nothing.
    for (auto const& block : extract::parse_document(text, "")) {
        if (block.kind != "code") continue;
        if (!block.lang.empty() && block.lang != "json") continue;
        auto parsed = nlohmann::json::parse(block.body, nullptr, false);
        if (parsed.is_discarded()) continue;
        append_call(calls, parsed, block.body);
    }
    return calls;
}

std::vector<std::string> to_argv(call const& value) {
    auto const* desc = find_prompt(value.name);
    if (desc == nullptr) throw usage_error{std::format("unknown tool '{}'\n", value.name)};
    if (desc->input == nullptr) {
        throw usage_error{std::format("'{}' is not a callable tool (it declares no input schema)\n", value.name)};
    }
    return schema::parse(*desc->input, value.input);
}

std::string format_result(call const& value, std::string_view output, int exit_code, bool ok) {
    std::string out = "### Tool: " + value.name + "\n";
    out += "Input: " + value.input.dump() + "\n";
    if (ok) {
        out += "Status: ok\n";
    } else {
        out += std::format("Status: failed (exit {})\n", exit_code);
    }
    out += '\n';
    if (!output.empty()) {
        out += output;
        if (!out.ends_with('\n')) out += '\n';
    }
    return out;
}

} // namespace prompt::tool_call
