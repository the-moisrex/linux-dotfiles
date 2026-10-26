#include "line_completion.hpp"
#include "completion.hpp"
#include "function.hpp"
#include "prompt_file.hpp"
#include "prompt_registry.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {
namespace {

// The dispatcher's own flags, offered only at the head (a step's flags come
// from its help text).
struct dispatcher_flag {
    std::string_view value;
    std::string_view description;
};

constexpr std::array<dispatcher_flag, 4> kDispatcherFlags = {
    {
        {.value = "--help", .description = "Show the dispatcher help"},
        {.value = "-h", .description = "Show the dispatcher help"},
        {.value = "--version", .description = "Print the version"},
        {.value = "--no-prompt-files", .description = "Do not inline *.prompt files"},
    },
};

// Where the cursor sits in the chain being typed.
struct cursor {
    bool opaque = false;            // inside an `.each` body, or past `.exec --`
    bool at_head = true;            // nothing but `--no-prompt-files` seen yet
    bool in_block = false;          // inside a `.func` body (`.each` bodies are opaque)
    bool has_step = false;          // a step (prompt or function) is open
    bool step_is_prompt = false;    // the open step is a compiled prompt
    bool signature_pending = false; // `.func` seen with its name still to type
    std::string_view step;          // the open step's name
    std::vector<std::string_view> args;
    bool no_prompt_files = false;
};

std::string_view step_name(std::string_view token) {
    if (!token.empty() && (token.front() == '.' || token.front() == '-')) return token.substr(1);
    return token;
}

// True when a token is starting the next step: once a step has been matched the
// chain reads only `.name`/`-name` as prompt names (a bare word is an argument),
// so a `.`-prefixed token is a step and a `-` one is a step unless it is a
// dispatcher option (`--head`, `-h`).
bool starts_step(std::string_view token) {
    if (token.empty()) return false;
    if (token.front() == '.') return true;
    return token.front() == '-' && token.size() >= 2 && token[1] != '-';
}

void write_dispatcher_flags(std::ostream& out, std::string_view current) {
    for (auto const& flag : kDispatcherFlags) {
        if (completion::matches(current, flag.value)) completion::write_item(out, flag.value, flag.description);
    }
}

// The same rules `parse_chain` applies, replayed far enough to know what the
// current token is being completed against: a step opens at a bare token while
// none has matched (the head) or at any `.`/`-` token naming a prompt or a
// function, its arguments follow, `--` ends the argument tail, an `.each` body
// is skipped (nested chains are not completed) and a `.func … .end` body is a
// chain of its own. `functions` are the names the expander would know here.
cursor locate(std::span<std::string_view const> prior, std::span<std::string_view const> functions) {
    cursor c;
    bool after_dash = false;
    bool skip_signature = false;
    std::vector<char> blocks; // 'e' = `.each` body, 'f' = `.func` body

    auto is_function = [&](std::string_view name) { return std::ranges::find(functions, name) != functions.end(); };

    for (auto const& token : prior) {
        if (token == "--no-prompt-files") {
            c.no_prompt_files = true;
            continue;
        }
        c.at_head = false;

        if (after_dash) continue;
        if (skip_signature) {
            skip_signature = false;
            continue;
        }

        if (!blocks.empty() && blocks.back() == 'e') {
            // Inside (or opening) an `.each` body: skip it all, counting nested
            // `.each` openers so their `.end` does not close the outer one.
            if (token == ".each") {
                blocks.push_back('e');
            } else if (token == ".end") {
                blocks.pop_back();
                c.has_step = false;
                c.step_is_prompt = false;
                c.step = {};
            }
            continue;
        }
        if (token == "--") {
            after_dash = true;
            continue;
        }
        if (token == ".func") {
            // The signature is the next token; its name is a definition, not an
            // argument, and the body that follows is a normal chain.
            blocks.push_back('f');
            skip_signature = true;
            c.has_step = false;
            c.step_is_prompt = false;
            c.step = {};
            continue;
        }
        if (token == ".end") {
            if (!blocks.empty()) blocks.pop_back();
            c.has_step = false;
            c.step_is_prompt = false;
            c.step = {};
            continue;
        }

        bool const prefixed = !token.empty() && (token.front() == '.' || token.front() == '-');
        std::string_view const name = step_name(token);
        if (prefixed || !c.has_step) {
            if (auto const* desc = find_prompt(name); desc != nullptr) {
                c.step = desc->name;
                c.has_step = true;
                c.step_is_prompt = true;
                c.args.clear();
                if (desc->takes_body) blocks.push_back('e');
                continue;
            }
            if (is_function(name)) {
                c.step = name;
                c.has_step = true;
                c.step_is_prompt = false;
                c.args.clear();
                continue;
            }
        }
        if (c.has_step) c.args.push_back(token);
    }

    c.signature_pending = skip_signature;
    c.opaque = after_dash || (!blocks.empty() && blocks.back() == 'e');
    c.in_block = !blocks.empty();
    return c;
}

} // namespace

void complete_line(std::string_view current, std::span<std::string_view const> prior,
                   std::filesystem::path const& git_root, std::ostream& out) {
    // Complete against the tokens the chain will actually run: an inlined
    // `*.prompt` file may define functions and open `.each`/`.func` bodies.
    bool const no_files = std::ranges::find(prior, std::string_view{"--no-prompt-files"}) != prior.end();
    auto const expanded = expand_prompt_file_tokens(prior, git_root, !no_files);
    std::vector<std::string_view> effective_views;
    std::span<std::string_view const> effective = prior;
    if (expanded.error.empty()) {
        effective_views.reserve(expanded.storage.size());
        for (auto const& token : expanded.storage) effective_views.push_back(token);
        effective = effective_views;
    }

    // The functions the expander would know here: the global `funcs` files plus
    // the `.func` blocks defined earlier on the line (or in an inlined file).
    function_table table;
    {
        std::string error;
        (void)load_function_files(table, git_root, error);
    }
    std::vector<function_def> functions = table.all();
    for (auto& def : collect_inline_functions(effective)) {
        bool const duplicate =
            std::ranges::any_of(functions, [&](function_def const& d) { return d.name == def.name; });
        if (!duplicate) functions.push_back(std::move(def));
    }
    std::vector<std::string_view> names;
    names.reserve(functions.size());
    std::vector<name_candidate> candidates;
    candidates.reserve(functions.size());
    for (auto const& def : functions) {
        names.push_back(def.name);
        candidates.push_back({.name = def.name, .description = function_signature(def)});
    }

    cursor const here = locate(effective, names);
    if (here.opaque || here.signature_pending) return;

    // A `.`/`-` token starts the next step wherever it appears; with no step
    // open yet we are at the head (or at the top of a `.func` body).
    if (starts_step(current) || !here.has_step) {
        if (here.at_head) write_dispatcher_flags(out, current);
        write_step_names(out, current, candidates);
        if (here.in_block) {
            if (completion::matches(current, ".end")) completion::write_item(out, ".end", "End the current block");
        } else if (completion::matches(current, ".func")) {
            completion::write_item(out, ".func", "Define a chain function");
        }
        if (here.at_head && !here.no_prompt_files) write_prompt_file_candidates(out, current, git_root);
        return;
    }

    auto const* desc = here.step_is_prompt ? find_prompt(here.step) : nullptr;
    if (desc == nullptr) return;
    write_prompt_candidates(*desc, current, here.args, out);
    // A `*.prompt` argument can appear anywhere; only offer the files when the
    // token is deliberately a path or a `.prompt` name, so a bare word stays a
    // word (fish's own path completion covers the rest).
    if (!here.no_prompt_files && (current.contains('/') || current.ends_with(".prompt"))) {
        write_prompt_file_candidates(out, current, git_root);
    }
}

} // namespace prompt
