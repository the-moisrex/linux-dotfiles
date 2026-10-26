#pragma once

#include <cstddef>
#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// Prompt files: `prompt debug.prompt` — the chain DSL the dispatcher already
// understands, kept in a text file instead of argv.
//
// Every argument token that names a readable `*.prompt` file is replaced, in
// place, by the tokens that file holds, so the rest of the dispatcher (chain
// parsing, sinks, `.each` bodies, history) only ever sees an ordinary argv.
//
// A file is split like a shell word list, with three differences: newlines are
// ordinary separators, `#` at the start of a token drops the rest of its line,
// and nothing is expanded (no `$VAR`, no globbing, no command substitution).
// `"…"` honours `\"` and `\\`, `'…'` is literal, and a backslash outside quotes
// makes the next character literal.
//
// A name is looked up next to the file that named it (the current directory at
// the top level), then in `<git-root>/.prompt/`, then in
// `$XDG_CONFIG_HOME/prompt/prompts` (default `~/.config/prompt/prompts`), so a
// nested `inner.prompt` means the one beside the file that referenced it. A
// resolved file may name another `*.prompt` file
// (at most kMaxPromptFileDepth deep; cycles are rejected), and a file whose
// first token is `prompt` may keep it — it is dropped.
//
// `--` stops the pass for the rest of its token sequence, so
// `.exec -- command x.prompt` is never rewritten. A `*.prompt` token that does
// not resolve is an error only as the first token of the invocation (where the
// `auto` fallback would otherwise silently eat it); everywhere else it stays an
// ordinary argument, so `.files missing.prompt` keeps the prompt's own warning.
//
// `expand = false` (`prompt --no-prompt-files …`) copies argv unchanged.
inline constexpr std::size_t kMaxPromptFileDepth = 8;

struct expanded_argv {
    std::vector<std::string> storage; // owns every token, argv[0] included
    std::string error;                // non-empty: print it and exit 2

    // The C-style argv view. Built on demand, never stored: `storage` moves
    // when this struct does, and a stored pointer vector would dangle (short
    // strings live inside the std::string object itself).
    [[nodiscard]] std::vector<char const*> argv() const {
        std::vector<char const*> out;
        out.reserve(storage.size());
        for (auto const& token : storage) out.push_back(token.c_str());
        return out;
    }
};

// True when `token` looks like a prompt file name (`.prompt` suffix with a
// non-empty stem).
bool has_prompt_file_suffix(std::string_view token);

// One text's worth of tokens, plus the lexer's one possible failure. Shared by
// `*.prompt` files and the `funcs` files that hold function definitions, so
// both read the same dialect (spaces/newlines split, `#` comments, quotes).
struct tokenized_text {
    std::vector<std::string> tokens;
    std::string error; // "<label>:<line>: unterminated \""
};

tokenized_text tokenize_prompt_text(std::string_view text, std::string_view label);

// The dispatcher's argv with every resolved `*.prompt` token inlined.
expanded_argv expand_prompt_files(std::span<char const* const> argv, std::filesystem::path const& git_root,
                                  bool expand = true);

// The same pass over a plain token list, with no `argv[0]` to preserve: every
// token is eligible for `*.prompt` inlining. Whole-line completion uses it to
// see the effective tokens a chain would run, so a function defined inside an
// inlined file is discovered and an `.each`/`.func` block coming from a file
// classifies correctly.
expanded_argv expand_prompt_file_tokens(std::span<std::string_view const> tokens, std::filesystem::path const& git_root,
                                        bool expand = true);

// Completion for a `*.prompt` argument, answered from the same three sources
// the resolver reads. A `current` without `/` lists the `.prompt` files of the
// current directory, then of the project `.prompt/` dir and the user config
// dir (a name found nearer wins, each candidate naming its source); one
// containing `/` lists that directory instead, offering its `.prompt` files and
// its subdirectories so the user can descend. Prints `value<TAB>description`
// lines.
void write_prompt_file_candidates(std::ostream& out, std::string_view current, std::filesystem::path const& git_root);

} // namespace prompt
