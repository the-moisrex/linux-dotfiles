#pragma once

// Shell-completion support for the native prompts. The fish completions call
//
//     prompt <name> --complete <prefix> [args typed before <prefix>...]
//
// which the dispatcher answers without running the prompt (see main.cpp).
// Every prompt gets its flag completions from its own help text; a prompt adds
// positional or value completions by registering a `complete_fn` (see
// prompt_registry.hpp) — the logic lives next to the prompt's argument parser,
// not in the shell script.
//
// Output is one candidate per line, `value<TAB>description` (the description
// is optional), the format `tse.resolve --complete` uses and fish reads.

#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace prompt {

struct completion_request {
    // The token being completed (may be empty).
    std::string_view prefix;
    // The prompt's arguments typed before the current token, in order.
    std::span<std::string_view const> prior_args;
};

using completion_fn = void (*)(completion_request const&, std::ostream&);

namespace completion {

struct item {
    std::string_view value;
    std::string_view description;
};

inline bool matches(std::string_view prefix, std::string_view value) {
    return prefix.empty() || value.starts_with(prefix);
}

inline void write_item(std::ostream& os, std::string_view value, std::string_view description = {}) {
    os << value;
    if (!description.empty()) os << '\t' << description;
    os << '\n';
}

inline void write_items(std::ostream& os, std::string_view prefix, std::span<item const> items) {
    for (auto const& it : items) {
        if (matches(prefix, it.value)) write_item(os, it.value, it.description);
    }
}

inline void write_values(std::ostream& os, std::string_view prefix, std::span<std::string_view const> values) {
    for (auto value : values) {
        if (matches(prefix, value)) write_item(os, value, {});
    }
}

// Emits `values` only when the previous argument is `flag`, so a prompt can
// complete `--format <TAB>`'s value without a state machine.
inline bool write_values_after_flag(std::ostream& os, completion_request const& req, std::string_view flag,
                                    std::span<std::string_view const> values) {
    if (req.prior_args.empty() || req.prior_args.back() != flag) return false;
    write_values(os, req.prefix, values);
    return true;
}

// Flag completion derived from the prompt's own `render_help_fn` output: the
// dash-leading options in the `Options:` block. This covers every prompt
// without per-file code; a `complete_fn` adds the positional/value candidates.
inline void write_help_flags(std::ostream& os, std::string_view prefix, std::string_view help) {
    if (prefix.empty() || prefix.front() != '-') return;
    auto options = help.find("Options:");
    if (options == std::string_view::npos) return;
    std::string_view body = help.substr(options);

    std::size_t pos = 0;
    while (pos < body.size()) {
        auto eol = body.find('\n', pos);
        std::string_view line = eol == std::string_view::npos ? body.substr(pos) : body.substr(pos, eol - pos);
        pos = eol == std::string_view::npos ? body.size() : eol + 1;

        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        line.remove_prefix(i);
        if (line.empty() || line.front() != '-') continue;

        // The flag column can hold several comma-separated spellings separated
        // by spaces (`-h, --help`, `--type, -t`); emit them until the
        // description starts (the first word that is not dash-led).
        std::size_t pos2 = 0;
        while (pos2 < line.size()) {
            while (pos2 < line.size() && (line[pos2] == ' ' || line[pos2] == '\t')) ++pos2;
            if (pos2 >= line.size()) break;
            auto end2 = line.find_first_of(" \t", pos2);
            std::string_view word =
                line.substr(pos2, end2 == std::string_view::npos ? std::string_view::npos : end2 - pos2);
            pos2 = end2 == std::string_view::npos ? line.size() : end2;
            if (!word.empty() && word.back() == ',') word.remove_suffix(1);
            if (word.size() < 2 || word.front() != '-') break;
            if (matches(prefix, word)) write_item(os, word, {});
        }
    }
}

} // namespace completion
} // namespace prompt
