#include "policy.hpp"
#include "errors.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::policy {

namespace {

constexpr std::size_t npos = std::string_view::npos;

bool glob_match(std::string_view pattern, std::string_view text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = npos;
    std::size_t mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool has_wildcard(std::string_view value) { return value.find_first_of("*?") != npos; }

bool program_matches(std::string_view rule, std::string_view program) {
    if (rule == "*" || rule == "**") return true;
    std::string const base = agent::program_name(program);
    return glob_match(rule, program) || glob_match(rule, base);
}

// Rule arguments are a prefix of the command's arguments (`allow git status`
// also permits `git status --short`); `**` is a final wildcard for the rest.
bool args_match(std::span<std::string const> patterns, std::span<std::string const> argv) {
    if (patterns.empty()) return true;
    std::size_t index = 1; // argv[0] is the program
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        if (patterns[i] == "**") return i + 1 == patterns.size();
        if (index >= argv.size()) return false;
        if (!glob_match(patterns[i], argv[index])) return false;
        ++index;
    }
    return true;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return text;
}

std::vector<std::string> words_of(std::string_view line) {
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) ++i;
        std::size_t const begin = i;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) == 0) ++i;
        if (i > begin) words.emplace_back(line.substr(begin, i - begin));
    }
    return words;
}

} // namespace

engine::engine(environment const& env) {
    // TSE tools are read-only data fetchers; md.extract only reads its input.
    for (auto const* program :
         {"tse", "tse.intraday", "tse.codal", "tse.find", "tse.resolve", "tse.snapshot", "tse.schema", "md.extract"}) {
        add(action::allow, program, {}, 0);
    }
    for (auto const* program : {"sudo", "su", "dd", "mkfs*", "shutdown", "reboot", "poweroff", "halt"}) {
        add(action::deny, program, {}, 0);
    }
    add(action::ask, "**", {}, 0);

    std::filesystem::path user_policy;
    if (char const* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        user_policy = std::filesystem::path(xdg) / "prompt" / "policy";
    } else if (char const* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        user_policy = std::filesystem::path(home) / ".config" / "prompt" / "policy";
    }
    if (!user_policy.empty()) load_file(user_policy, 1);
    if (!env.git_root.empty()) load_file(env.git_root / ".prompt" / "policy", 2);

    parse_env("PROMPT_POLICY_ALLOW", action::allow, 3);
    parse_env("PROMPT_POLICY_DENY", action::deny, 3);
    // Grants passed by a parent `.each`: static programs it approved once.
    parse_env("PROMPT_INNER_ALLOW", action::allow, 0);
}

void engine::add(action verdict, std::string program, std::vector<std::string> args, int source) {
    std::size_t specificity = 0;
    if (!program.empty() && !has_wildcard(program)) ++specificity;
    for (auto const& arg : args) {
        if (arg != "*" && arg != "**" && !has_wildcard(arg)) ++specificity;
    }
    rules_.push_back(rule{
        .verdict = verdict,
        .program = std::move(program),
        .args = std::move(args),
        .source = source,
        .specificity = specificity,
    });
}

void engine::load_file(std::filesystem::path const& path, int source) {
    std::ifstream file(path);
    if (!file) return;
    std::string line;
    std::size_t number = 0;
    while (std::getline(file, line)) {
        ++number;
        std::string_view text = trim(line);
        if (text.empty() || text.front() == '#') continue;
        auto words = words_of(text);
        if (words.size() < 2) {
            std::fprintf(stderr, "prompt: policy %s:%zu: expected 'allow|ask|deny PROGRAM [ARGS...]'\n", path.c_str(),
                         number);
            continue;
        }
        action verdict = action::ask;
        if (words[0] == "allow") {
            verdict = action::allow;
        } else if (words[0] == "deny") {
            verdict = action::deny;
        } else if (words[0] == "ask") {
            verdict = action::ask;
        } else {
            std::fprintf(stderr, "prompt: policy %s:%zu: unknown action '%s'\n", path.c_str(), number,
                         words[0].c_str());
            continue;
        }
        std::vector<std::string> args(words.begin() + 2, words.end());
        add(verdict, words[1], std::move(args), source);
    }
}

void engine::parse_env(std::string_view name, action verdict, int source) {
    char const* value = std::getenv(std::string(name).c_str());
    if (value == nullptr) return;
    std::string_view text(value);
    while (!text.empty()) {
        auto const comma = text.find(',');
        std::string_view part = text.substr(0, comma);
        text = comma == npos ? std::string_view{} : text.substr(comma + 1);
        part = trim(part);
        if (!part.empty()) add(verdict, std::string(part), {}, source);
    }
}

engine::action engine::decide(std::span<std::string const> argv) const {
    if (argv.empty()) return action::ask;
    rule const* best = nullptr;
    for (auto const& candidate : rules_) {
        if (!program_matches(candidate.program, argv[0])) continue;
        if (!args_match(candidate.args, argv)) continue;
        if (best == nullptr) {
            best = &candidate;
            continue;
        }
        bool better = false;
        if (candidate.specificity != best->specificity) {
            better = candidate.specificity > best->specificity;
        } else if (candidate.source != best->source) {
            better = candidate.source > best->source;
        } else if (candidate.verdict != best->verdict) {
            // deny wins a full tie; then allow; then ask.
            better = candidate.verdict == action::deny ||
                     (candidate.verdict == action::allow && best->verdict == action::ask);
        }
        if (better) best = &candidate;
    }
    return best == nullptr ? action::ask : best->verdict;
}

void engine::require(std::span<std::string const> argv, std::string_view label) {
    if (argv.empty()) throw usage_error{"prompt: empty command\n"};
    std::string const display = label.empty() ? agent::join_argv(argv) : std::string(label);

    action const verdict = decide(argv);
    if (verdict == action::allow) return;
    if (verdict == action::deny) {
        throw soft_error{"prompt: command denied by policy: " + display +
                         "\n(edit ~/.config/prompt/policy or .prompt/policy to change this)\n"};
    }
    if (yes_ || std::getenv("PROMPT_INNER_YES") != nullptr) return;

    std::FILE* tty = std::fopen("/dev/tty", "r+");
    if (tty == nullptr) {
        throw soft_error{"prompt: command needs approval but no terminal is available: " + display +
                         "\n(add an `allow` rule to ~/.config/prompt/policy, or pass --yes)\n"};
    }
    std::fprintf(tty, "prompt: run this command?\n  %s\n[y]es / [n]o / [a]lways / [d]eny > ", display.c_str());
    std::fflush(tty);
    std::array<char, 64> line{};
    bool const read = std::fgets(line.data(), static_cast<int>(line.size()), tty) != nullptr;
    std::fclose(tty);
    if (!read) throw soft_error{"prompt: command cancelled: " + display + "\n"};

    char const answer = static_cast<char>(std::tolower(static_cast<unsigned char>(line[0])));
    if (answer == 'y') return;
    if (answer == 'a') {
        add(action::allow, agent::program_name(argv[0]), {}, 4);
        return;
    }
    if (answer == 'd') {
        add(action::deny, agent::program_name(argv[0]), {}, 4);
        throw soft_error{"prompt: command denied for this session: " + display + "\n"};
    }
    throw soft_error{"prompt: command cancelled: " + display + "\n"};
}

void engine::require_chain(std::span<std::string const> chain_tokens) {
    std::vector<std::string> argv;
    argv.reserve(chain_tokens.size() + 1);
    argv.emplace_back("prompt");
    for (auto const& token : chain_tokens) argv.push_back(token);
    require(argv, agent::join_argv(argv));
}

} // namespace prompt::policy
