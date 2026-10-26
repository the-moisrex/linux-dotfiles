#pragma once

#include "../core/process.hpp"
#include "stage.hpp"
#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::agent {

// The selector token grammar shared by `.extract`, `.each` and `.exec`:
// kind[:qualifier][@N], plus the bare `auto`/`any`/`all`/`none` forms. A bare
// word that is not one of these is a section heading for `.extract`; `.exec`
// treats it as a command, so a section selector there needs `section:`.
[[nodiscard]] bool is_selector_token(std::string_view token);

// One selection in an expect plan. `name` is empty for the primary (the entry
// whose result replaces the chain context); a named entry is published as that
// fact key by `.extract` (e.g. `save.stem=field:filename`).
struct expect_entry {
    std::string name;
    std::string selector;
};

// The plan grammar shared by `.expect` and `.extract`: entries separated by
// newlines, each `[name=]selector`. `name` must look like a fact key
// (`^[A-Za-z_][A-Za-z0-9_.-]*$`); anything else is part of the selector, so a
// selector containing `=`, `:` or spaces stays intact. Empty lines are
// ignored. A single bare selector parses as one unnamed entry.
[[nodiscard]] std::vector<expect_entry> parse_expect_plan(std::string_view plan);

// Serialize entries back to a plan string (newline-joined, no trailing newline).
[[nodiscard]] std::string format_expect_plan(std::span<expect_entry const> entries);

// The unnamed primary selector of a plan, for `.each`/`.exec`, which extract a
// single value. A plan with no unnamed entry uses its only entry when there is
// exactly one; otherwise there is no unambiguous primary and this throws
// usage_error.
[[nodiscard]] std::string primary_selector(std::string_view plan);

// Split a command line into argv without a shell: single quotes, double quotes
// and backslash work; expansions and compound syntax (| & ; < > ` $( ) are
// refused, so a command extracted from an AI can never become a shell script.
// Throws usage_error on malformed or compound input.
[[nodiscard]] std::vector<std::string> split_command(std::string_view line);

// Human-readable shell-quoting for the approval prompt and `--print`.
[[nodiscard]] std::string join_argv(std::span<std::string const> argv);

// `.exec`'s CLI. Flags are read until the first non-flag token; after that
// everything is the command, so `--version` reaches the program unchanged.
struct exec_spec {
    bool stdin_data = false;
    bool print_only = false;
    bool yes = false;
    std::chrono::seconds timeout{600};
    std::string selector; // empty: extract from the context
    std::vector<std::string> command;
};

[[nodiscard]] exec_spec parse_exec_args(std::span<std::string_view const> args);

// The statically-named `.exec` commands in a prompt chain (the tokens after
// `prompt`), including nested `.each` bodies and `prompt` chains run through
// `.exec`. A `{placeholder}` program is dynamic and is not reported: the child
// checks it when the value exists.
[[nodiscard]] std::vector<std::vector<std::string>> static_exec_commands(std::span<std::string const> chain_tokens);

// Validate a loop body template before any item is substituted, including
// nested bodies. Throws usage_error on a malformed `.exec` (e.g. a selector
// mixed with a command). A `{placeholder}` program is allowed: it is dynamic,
// so it is not pre-approved and the child's policy checks the resolved
// program.
void validate_exec_template(std::span<std::string const> chain_tokens);

// Basename of a program token (`/usr/bin/git` -> `git`).
[[nodiscard]] std::string program_name(std::string_view program);

} // namespace prompt::agent
