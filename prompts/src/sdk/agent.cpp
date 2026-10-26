#include "agent.hpp"
#include "chaining.hpp"
#include "errors.hpp"
#include "prompt_registry.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::agent {

namespace {

bool plain_char(char c) {
    auto const uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) != 0) return true;
    return std::string_view("_@%+=:,./-").contains(c);
}

std::string quote_word(std::string_view word) {
    if (!word.empty() && std::ranges::all_of(word, plain_char)) return std::string(word);
    std::string out = "'";
    for (char c : word) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += '\'';
    return out;
}

void walk_tokens(std::span<std::string const> tokens, std::vector<std::vector<std::string>>& commands);

// NOLINTNEXTLINE(misc-no-recursion) — a body is a tree; the depth is bounded by the parse.
void walk_invocation(prompt::prompt_invocation const& inv, std::vector<std::vector<std::string>>& commands) {
    if (inv.name == "each" && !inv.body.empty()) {
        std::vector<std::string> body;
        body.reserve(inv.body.size());
        for (auto token : inv.body) body.emplace_back(token);
        walk_tokens(body, commands);
    }
    if (inv.name != "exec") return;
    try {
        auto spec = parse_exec_args(inv.args);
        if (spec.command.empty()) return;
        std::string program = program_name(spec.command.front());
        commands.push_back(spec.command);
        if (program == "prompt") {
            std::vector<std::string> inner(spec.command.begin() + 1, spec.command.end());
            walk_tokens(inner, commands);
        }
    } catch (prompt::prompt_error const&) { // NOLINT(bugprone-empty-catch) — the child reports it.
    }
}

// NOLINTNEXTLINE(misc-no-recursion) — walks nested `.each` bodies.
void walk_tokens(std::span<std::string const> tokens, std::vector<std::vector<std::string>>& commands) {
    std::vector<char const*> argv;
    argv.reserve(tokens.size() + 1);
    argv.push_back("prompt");
    for (auto const& token : tokens) argv.push_back(token.c_str());
    auto chain = prompt::parse_chain(argv, prompt::get_all_prompts());
    if (!chain.error.empty()) return;
    for (auto const& inv : chain.invocations) walk_invocation(inv, commands);
}

// NOLINTNEXTLINE(misc-no-recursion) — validates nested `.each` bodies.
void validate_tokens(std::span<std::string const> tokens) {
    std::vector<char const*> argv;
    argv.reserve(tokens.size() + 1);
    argv.push_back("prompt");
    for (auto const& token : tokens) argv.push_back(token.c_str());
    auto chain = prompt::parse_chain(argv, prompt::get_all_prompts());
    if (!chain.error.empty()) throw usage_error{chain.error + "\n"};

    for (auto const& inv : chain.invocations) {
        if (inv.name == "each" && !inv.body.empty()) {
            std::vector<std::string> body;
            body.reserve(inv.body.size());
            for (auto token : inv.body) body.emplace_back(token);
            validate_tokens(body);
        }
        if (inv.name != "exec") continue;
        auto spec = parse_exec_args(inv.args);
        if (!spec.command.empty() && program_name(spec.command.front()) == "prompt") {
            std::vector<std::string> inner(spec.command.begin() + 1, spec.command.end());
            validate_tokens(inner);
        }
    }
}

} // namespace

bool is_selector_token(std::string_view token) {
    static constexpr std::array<std::string_view, 9> bare = {
        "auto", "any", "all", "none", "code", "table", "section", "command", "frontmatter",
    };
    if (std::ranges::any_of(bare, [&](std::string_view kind) { return token == kind; })) return true;
    static constexpr std::array<std::string_view, 6> prefixes = {
        "code:", "table:", "section:", "command:", "frontmatter:", "field:",
    };
    return std::ranges::any_of(prefixes, [&](std::string_view prefix) { return token.starts_with(prefix); });
}

namespace {

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

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return text;
}

} // namespace

std::vector<expect_entry> parse_expect_plan(std::string_view plan) {
    std::vector<expect_entry> entries;
    if (plan.empty()) return entries;
    std::size_t begin = 0;
    while (begin <= plan.size()) {
        auto const end = plan.find('\n', begin);
        std::string_view line =
            trim(std::string_view{plan.data() + begin, (end == std::string_view::npos ? plan.size() : end) - begin});
        if (!line.empty()) {
            expect_entry entry;
            auto const equals = line.find('=');
            auto const name = equals == std::string_view::npos ? std::string_view{} : trim(line.substr(0, equals));
            if (valid_fact_key(name)) {
                entry.name = std::string(name);
                entry.selector = std::string(trim(line.substr(equals + 1)));
            } else {
                entry.selector = std::string(line);
            }
            if (!entry.selector.empty()) entries.push_back(std::move(entry));
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return entries;
}

std::string format_expect_plan(std::span<expect_entry const> entries) {
    std::string out;
    for (auto const& entry : entries) {
        if (!out.empty()) out += '\n';
        if (!entry.name.empty()) {
            out += entry.name;
            out += '=';
        }
        out += entry.selector;
    }
    return out;
}

std::string primary_selector(std::string_view plan) {
    auto const entries = parse_expect_plan(plan);
    for (auto const& entry : entries) {
        if (entry.name.empty()) return entry.selector;
    }
    if (entries.size() == 1) return entries.front().selector;
    throw usage_error{"prompt: the expect plan has no primary selector (an unnamed `[name=]selector` entry)\n"};
}

std::vector<std::string> split_command(std::string_view line) {
    std::vector<std::string> words;
    std::string current;
    bool in_word = false;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char const c = line[i];
        if (quote != 0) {
            if (c == quote) {
                quote = 0;
                in_word = true;
                continue;
            }
            if (quote == '"' && c == '\\' && i + 1 < line.size()) {
                char const next = line[++i];
                if (next == '"' || next == '\\' || next == '$' || next == '`') {
                    current += next;
                } else {
                    current += '\\';
                    current += next;
                }
                in_word = true;
                continue;
            }
            current += c;
            in_word = true;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            in_word = true;
            continue;
        }
        if (c == '\\' && i + 1 < line.size()) {
            current += line[++i];
            in_word = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (in_word) {
                words.push_back(current);
                current.clear();
                in_word = false;
            }
            continue;
        }
        if (std::string_view("|&;<>`").contains(c) || (c == '$' && i + 1 < line.size() && line[i + 1] == '(')) {
            throw usage_error{"prompt: compound shell syntax is not supported (found '" + std::string(1, c) +
                              "'); use a `prompt` command or a single program with arguments\n"};
        }
        current += c;
        in_word = true;
    }
    if (quote != 0) throw usage_error{"prompt: unterminated quote in command\n"};
    if (in_word) words.push_back(current);
    return words;
}

std::string join_argv(std::span<std::string const> argv) {
    std::string out;
    for (auto const& word : argv) {
        if (!out.empty()) out += ' ';
        out += quote_word(word);
    }
    return out;
}

exec_spec parse_exec_args(std::span<std::string_view const> args) {
    exec_spec spec;
    bool literal = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view const arg = args[i];
        if (!spec.command.empty()) {
            spec.command.emplace_back(arg);
            continue;
        }
        if (literal) {
            spec.command.emplace_back(arg);
            continue;
        }
        if (arg == "--") {
            literal = true;
            continue;
        }
        if (arg == "--stdin") {
            spec.stdin_data = true;
            continue;
        }
        if (arg == "--print") {
            spec.print_only = true;
            continue;
        }
        if (arg == "--yes") {
            spec.yes = true;
            continue;
        }
        if (arg == "--timeout" || arg.starts_with("--timeout=")) {
            std::string_view value;
            if (arg == "--timeout") {
                if (++i >= args.size()) throw usage_error{"prompt exec: --timeout needs a value\n"};
                value = args[i];
            } else {
                value = arg.substr(std::string_view("--timeout=").size());
            }
            unsigned long long seconds = 0;
            auto const* begin = value.data();
            auto const* end = value.data() + value.size();
            auto [ptr, ec] = std::from_chars(begin, end, seconds);
            if (ec != std::errc{} || ptr != end || seconds == 0) {
                throw usage_error{"prompt exec: --timeout needs a positive number of seconds\n"};
            }
            spec.timeout = std::chrono::seconds(seconds);
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            throw usage_error{"prompt exec: unknown option " + std::string(arg) + "\n"};
        }
        if (spec.selector.empty() && is_selector_token(arg)) {
            spec.selector = std::string(arg);
            continue;
        }
        spec.command.emplace_back(arg);
    }
    if (!spec.selector.empty() && !spec.command.empty()) {
        throw usage_error{"prompt exec: use either a selector or a command, not both\n"};
    }
    return spec;
}

std::string program_name(std::string_view program) {
    auto const slash = program.find_last_of('/');
    return std::string(slash == std::string_view::npos ? program : program.substr(slash + 1));
}

std::vector<std::vector<std::string>> static_exec_commands(std::span<std::string const> chain_tokens) {
    std::vector<std::vector<std::string>> commands;
    walk_tokens(chain_tokens, commands);
    return commands;
}

void validate_exec_template(std::span<std::string const> chain_tokens) { validate_tokens(chain_tokens); }

} // namespace prompt::agent
