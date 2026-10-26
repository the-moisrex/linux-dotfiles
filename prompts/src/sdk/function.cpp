#include "function.hpp"

#include "prompt_registry.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace prompt {
namespace {

using string_list = std::vector<std::string>;

bool is_reserved_name(std::string_view name) { return name == "func" || name == "end" || name == "prompt"; }

bool name_is_valid(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '-';
    });
}

// The two prompt-name spellings (`.name` and `-name`); the bare first-token
// form is decided by the caller, exactly like `parse_chain` does.
std::optional<std::string_view> dotted_name(std::string_view token) {
    if (token.size() > 1 && token.front() == '.') return token.substr(1);
    if (token.size() > 1 && token.front() == '-' && token[1] != '-' &&
        std::isalnum(static_cast<unsigned char>(token[1])) != 0)
        return token.substr(1);
    return std::nullopt;
}

bool is_step(function_table const& table, std::string_view name) {
    return find_prompt(name) != nullptr || table.contains(name);
}

// A function call's arguments run until the next step, so a following `.note`
// (or function) is not swallowed.
bool is_boundary(std::string_view token, function_table const& table) {
    if (token == ".end" || token == ".func" || token == "--") return true;
    if (auto const name = dotted_name(token)) return is_step(table, *name);
    return false;
}

struct expand_context {
    function_table& table;
    bool allow_chain;
    bool override_ok;
    string_list active;       // functions being expanded (cycle guard)
    string_list defined_here; // names defined in this top-level sequence
    int body_depth = 0;       // open compiled bodies (`.each` … `.end`)
    std::string error;

    expand_context(function_table& table_ref, bool chain, bool overrides)
        : table(table_ref), allow_chain(chain), override_ok(overrides) {}
};

bool parse_signature(std::string_view token, function_def& def, std::string& error) {
    auto const open = token.find('(');
    if (open == std::string_view::npos) {
        def.name = std::string(token);
    } else {
        if (token.back() != ')') {
            error = "malformed function signature '" + std::string(token) + "'";
            return false;
        }
        def.name = std::string(token.substr(0, open));
        std::string_view const inner = token.substr(open + 1, token.size() - open - 2);
        std::size_t start = 0;
        while (!inner.empty()) {
            auto const comma = inner.find(',', start);
            std::string_view const piece =
                comma == std::string_view::npos ? inner.substr(start) : inner.substr(start, comma - start);
            if (piece.empty()) {
                error = "empty parameter in '" + std::string(token) + "'";
                return false;
            }
            function_param param;
            auto const equals = piece.find('=');
            if (equals == std::string_view::npos) {
                param.name = std::string(piece);
            } else {
                param.name = std::string(piece.substr(0, equals));
                param.default_value = std::string(piece.substr(equals + 1));
                param.has_default = true;
            }
            if (!name_is_valid(param.name)) {
                error = "invalid parameter '" + param.name + "' in '" + std::string(token) + "'";
                return false;
            }
            if (std::ranges::any_of(def.params, [&](function_param const& p) { return p.name == param.name; })) {
                error = "duplicate parameter '" + param.name + "' in '" + std::string(token) + "'";
                return false;
            }
            def.params.push_back(std::move(param));
            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }
    }
    if (!name_is_valid(def.name)) {
        error = "invalid function name '" + def.name + "'";
        return false;
    }
    return true;
}

// The index of the `.end` that closes a `.func` body, nesting compiled bodies
// (`.each` … `.end`) so their `.end` does not close the definition.
std::optional<std::size_t> find_block_end(string_list const& tokens, std::size_t start) {
    int depth = 1;
    for (std::size_t k = start; k < tokens.size(); ++k) {
        if (tokens[k] == ".end") {
            if (--depth == 0) return k;
            continue;
        }
        if (auto const name = dotted_name(tokens[k])) {
            if (auto const* desc = find_prompt(*name); desc != nullptr && desc->takes_body) ++depth;
        }
    }
    return std::nullopt;
}

// Bind a call's arguments to the definition's parameters and substitute the
// body. The `{placeholder}` names that are not parameters are left untouched so
// a function can carry `.each` placeholders through.
bool substitute(function_def const& def, string_list const& args, string_list& out, std::string& error) {
    string_list positionals;
    string_list values(def.params.size());
    std::vector<bool> bound(def.params.size(), false);

    for (std::size_t k = 0; k < args.size(); ++k) {
        std::string_view const arg = args[k];
        if (!arg.starts_with("--") || arg.size() <= 2) {
            positionals.emplace_back(arg);
            continue;
        }
        std::string_view key = arg.substr(2);
        std::string_view value;
        if (auto const equals = key.find('='); equals != std::string_view::npos) {
            value = key.substr(equals + 1);
            key = key.substr(0, equals);
        } else {
            if (k + 1 >= args.size()) {
                error = "parameter --" + std::string(key) + " needs a value";
                return false;
            }
            value = args[++k];
        }
        std::size_t index = def.params.size();
        for (std::size_t p = 0; p < def.params.size(); ++p) {
            if (def.params[p].name == key) {
                index = p;
                break;
            }
        }
        if (index == def.params.size()) {
            error = "." + def.name + " has no parameter '" + std::string(key) + "'";
            return false;
        }
        if (bound[index]) {
            error = "parameter --" + std::string(key) + " is given twice";
            return false;
        }
        values[index] = std::string(value);
        bound[index] = true;
    }

    std::size_t used = 0;
    for (std::size_t p = 0; p < def.params.size(); ++p) {
        if (bound[p]) continue;
        if (used < positionals.size()) {
            values[p] = positionals[used++];
            bound[p] = true;
        } else if (def.params[p].has_default) {
            values[p] = def.params[p].default_value;
            bound[p] = true;
        } else {
            error = "." + def.name + " needs argument '" + def.params[p].name + "'";
            return false;
        }
    }
    string_list const extras(positionals.begin() + static_cast<std::ptrdiff_t>(used), positionals.end());

    for (auto const& token : def.body) {
        if (token == "{@}") {
            out.insert(out.end(), extras.begin(), extras.end());
            continue;
        }
        std::string expanded;
        for (std::size_t i = 0; i < token.size();) {
            if (token[i] != '{') {
                expanded += token[i++];
                continue;
            }
            auto const close = token.find('}', i + 1);
            if (close == std::string::npos) {
                expanded += token[i++];
                continue;
            }
            std::string_view const name(token.data() + i + 1, close - i - 1);
            bool replaced = false;
            if (name == "0") {
                expanded += def.name;
                replaced = true;
            } else if (!name.empty() && std::ranges::all_of(name, [](char c) {
                           return std::isdigit(static_cast<unsigned char>(c)) != 0;
                       })) {
                std::size_t index = 0;
                for (char const c : name) index = (index * 10) + static_cast<std::size_t>(c - '0');
                if (index >= 1 && index <= positionals.size()) {
                    expanded += positionals[index - 1];
                    replaced = true;
                } else {
                    error = "." + def.name + " has no argument {" + std::string(name) + "}";
                    return false;
                }
            } else {
                for (std::size_t p = 0; p < def.params.size(); ++p) {
                    if (def.params[p].name == name) {
                        expanded += values[p];
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced) expanded.append(token, i, close - i + 1);
            i = close + 1;
        }
        out.push_back(std::move(expanded));
    }
    return true;
}

// Calls recurse, but the cycle guard and kMaxFunctionDepth bound it.
// NOLINTNEXTLINE(misc-no-recursion)
bool sequence(string_list const& tokens, string_list& out, expand_context& ctx) {
    for (std::size_t i = 0; i < tokens.size();) {
        std::string const& token = tokens[i];

        if (token == ".func") {
            if (ctx.body_depth != 0 || !ctx.active.empty()) {
                ctx.error = "prompt: .func is only allowed at the top level";
                return false;
            }
            if (i + 1 >= tokens.size()) {
                ctx.error = "prompt: .func needs a name";
                return false;
            }
            function_def def;
            if (!parse_signature(tokens[i + 1], def, ctx.error)) {
                ctx.error = "prompt: " + ctx.error;
                return false;
            }
            auto const end = find_block_end(tokens, i + 2);
            if (!end) {
                ctx.error = "prompt: function '" + def.name + "' is missing .end";
                return false;
            }
            def.body.assign(tokens.begin() + static_cast<std::ptrdiff_t>(i + 2),
                            tokens.begin() + static_cast<std::ptrdiff_t>(*end));
            def.source = "the command line";
            std::string const name = def.name;
            if (std::ranges::find(ctx.defined_here, name) != ctx.defined_here.end()) {
                ctx.error = "prompt: function '" + name + "' is defined twice in one place";
                return false;
            }
            if (!ctx.table.add(std::move(def), ctx.override_ok, ctx.error)) return false;
            ctx.defined_here.push_back(name);
            i = *end + 1;
            continue;
        }

        if (token == "--") {
            // Explicit end of expansion: the rest belongs to another tool
            // (`.exec -- …`) and must reach it untouched.
            out.insert(out.end(), tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.end());
            return true;
        }

        if (token == ".end") {
            if (ctx.body_depth == 0) {
                ctx.error = "prompt: unexpected .end";
                return false;
            }
            --ctx.body_depth;
            out.push_back(token);
            ++i;
            continue;
        }

        if (!ctx.allow_chain) {
            ctx.error = "prompt: unexpected '" + token + "' in a function file (only .func blocks)";
            return false;
        }

        bool const at_start = i == 0 && ctx.active.empty();
        std::optional<std::string_view> call;
        if (token.size() > 1 && token.front() == '.') call = std::string_view(token).substr(1);
        else if (at_start && !token.empty() && std::isalnum(static_cast<unsigned char>(token.front())) != 0)
            call = token;

        if (call && ctx.table.contains(*call)) {
            function_def const* def = ctx.table.find(*call);
            std::size_t j = i + 1;
            string_list args;
            while (j < tokens.size() && !is_boundary(tokens[j], ctx.table)) {
                args.push_back(tokens[j]);
                ++j;
            }
            if (std::ranges::find(ctx.active, *call) != ctx.active.end()) {
                ctx.error = "prompt: function recursion in ." + std::string(*call);
                return false;
            }
            if (ctx.active.size() >= kMaxFunctionDepth) {
                ctx.error = "prompt: functions nested too deep";
                return false;
            }
            string_list substituted;
            if (!substitute(*def, args, substituted, ctx.error)) {
                ctx.error = "prompt: " + ctx.error;
                return false;
            }
            ctx.active.emplace_back(*call);
            bool const ok = sequence(substituted, out, ctx);
            ctx.active.pop_back();
            if (!ok) return false;
            i = j;
            continue;
        }

        if (auto const name = dotted_name(token)) {
            if (auto const* desc = find_prompt(*name); desc != nullptr && desc->takes_body) ++ctx.body_depth;
        }
        out.push_back(token);
        ++i;
    }

    if (ctx.body_depth != 0) {
        ctx.error = "prompt: missing .end";
        return false;
    }
    return true;
}

std::optional<std::string> read_file_text(std::filesystem::path const& path, std::string& error) {
    errno = 0;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        int const code = errno;
        error = "prompt: cannot read function file '" + path.string() +
                "': " + (code == 0 ? std::string("cannot open") : std::string(std::strerror(code)));
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::optional<std::filesystem::path> config_funcs_path() {
    if (char const* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "prompt" / "funcs";
    }
    if (char const* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "prompt" / "funcs";
    }
    return std::nullopt;
}

bool load_function_file(function_table& table, std::filesystem::path const& path, std::string& error) {
    std::string read_error;
    auto const text = read_file_text(path, read_error);
    if (!text) {
        error = read_error;
        return false;
    }
    auto const lexed = tokenize_prompt_text(*text, path.string());
    if (!lexed.error.empty()) {
        error = "prompt: " + lexed.error;
        return false;
    }
    // A funcs file is definitions only, and a later source replaces an earlier.
    expand_context ctx{table, false, true};
    string_list ignored;
    if (!sequence(lexed.tokens, ignored, ctx)) {
        error = "prompt: " + path.string() + ": " + ctx.error;
        return false;
    }
    return true;
}

} // namespace

function_def const* function_table::find(std::string_view name) const {
    for (auto const& def : defs_) {
        if (def.name == name) return &def;
    }
    return nullptr;
}

bool function_table::contains(std::string_view name) const { return find(name) != nullptr; }

bool function_table::add(function_def def, bool override_ok, std::string& error) {
    if (is_reserved_name(def.name)) {
        error = "prompt: '" + def.name + "' is reserved and cannot be a function";
        return false;
    }
    if (find_prompt(def.name) != nullptr) {
        error = "prompt: '" + def.name + "' is a built-in step; a function cannot shadow it";
        return false;
    }
    for (auto& existing : defs_) {
        if (existing.name != def.name) continue;
        if (!override_ok) {
            error = "prompt: function '" + def.name + "' is already defined (" + existing.source + ")";
            return false;
        }
        existing = std::move(def);
        return true;
    }
    defs_.push_back(std::move(def));
    return true;
}

std::vector<function_def> collect_inline_functions(std::span<std::string_view const> tokens) {
    std::vector<function_def> out;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i] != ".func") continue;
        function_def def;
        std::string error;
        if (!parse_signature(tokens[i + 1], def, error)) continue; // lenient: skip a bad definition
        // The matching `.end`, nesting compiled bodies (`.each` … `.end`) so
        // theirs does not close the definition, exactly like `find_block_end`.
        std::size_t depth = 1;
        std::size_t k = i + 2;
        for (; k < tokens.size(); ++k) {
            if (tokens[k] == ".end") {
                if (--depth == 0) break;
                continue;
            }
            if (auto const name = dotted_name(tokens[k])) {
                if (auto const* desc = find_prompt(*name); desc != nullptr && desc->takes_body) ++depth;
            }
        }
        if (k >= tokens.size()) break; // unterminated: nothing after it can be trusted
        out.push_back(std::move(def));
        i = k;
    }
    return out;
}

std::string function_signature(function_def const& def) {
    if (def.params.empty()) return "function";
    std::string out = "function(";
    for (std::size_t i = 0; i < def.params.size(); ++i) {
        if (i != 0) out += ", ";
        out += def.params[i].name;
        if (def.params[i].has_default) {
            out += '=';
            out += def.params[i].default_value;
        }
    }
    out += ')';
    return out;
}

bool load_function_files(function_table& table, std::filesystem::path const& git_root, std::string& error) {
    string_list files;
    if (char const* env = std::getenv("PROMPT_FUNCS"); env != nullptr) {
        if (*env == '\0') return true;
        std::string_view const list(env);
        std::size_t start = 0;
        while (true) {
            auto const colon = list.find(':', start);
            auto const part = colon == std::string_view::npos ? list.substr(start) : list.substr(start, colon - start);
            if (!part.empty()) files.emplace_back(part);
            if (colon == std::string_view::npos) break;
            start = colon + 1;
        }
    } else {
        if (auto const user = config_funcs_path()) files.push_back(user->string());
        if (!git_root.empty()) files.push_back((git_root / ".prompt" / "funcs").string());
    }
    for (auto const& file : files) {
        std::filesystem::path const path{file};
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) continue;
        if (!load_function_file(table, path, error)) return false;
    }
    return true;
}

expanded_argv expand_functions(std::span<std::string const> argv, function_table& table, bool allow_chain) {
    expanded_argv out;
    if (argv.empty()) return out;
    out.storage.emplace_back(argv.front());
    string_list const rest(argv.begin() + 1, argv.end());
    expand_context ctx{table, allow_chain, false};
    if (!sequence(rest, out.storage, ctx)) {
        out.error = ctx.error;
        out.storage.clear();
    }
    return out;
}

} // namespace prompt
