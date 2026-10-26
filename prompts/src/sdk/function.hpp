#pragma once

#include "prompt_file.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// Functions: the third kind of step. A prompt and a sink are compiled in; a
// function is a named, parameterized chain template the user defines, and it is
// invoked with `.name` exactly like a prompt is.
//
//     .func fix(task, ai=dseek)
//         .note "{task}"
//         .ai.{ai}
//     .end
//
// The definition is a `.func NAME(params) … .end` block. The signature is one
// token, so params are unambiguous even though a body may start with a bare
// token: `NAME` or `NAME(p, p=default, …)` (no spaces inside the parens). A
// call binds declared params by position (`{name}`), by `--name value`, or by
// `--name=value`; a required param with no argument is an error, a defaulted
// one falls back to its default, and positionals past the declared params are
// `{@}`. `{0}` is the function name, `{N}` the Nth positional argument. A
// `{placeholder}` that is not a declared param or a special is left alone, so a
// function composes with `.each` (`{symbol}`, `{item}`, …).
//
// Definitions live in the command itself, in a `.prompt` file, or in a `funcs`
// file (`<git-root>/.prompt/funcs`, `$XDG_CONFIG_HOME/prompt/funcs`; project
// overrides user per name). A function may not shadow a compiled prompt or sink
// and may not be redefined in the same source — both are hard errors. Calls and
// definitions are expanded to ordinary tokens in `main.cpp` after the
// `*.prompt` pass and before `parse_chain`, so sinks, `.each` bodies and the
// history store only ever see a normal argv.
//
// A function body may not define a function (`.func` is top level only), may
// not recurse (directly or through another function), and must be defined
// before it is called.
inline constexpr std::size_t kMaxFunctionDepth = 16;

struct function_param {
    std::string name;
    std::string default_value; // meaningful only when has_default
    bool has_default = false;
};

struct function_def {
    std::string name;
    std::vector<function_param> params;
    std::vector<std::string> body; // template tokens, substituted at the call
    std::string source;            // where it was defined, for error messages
};

// The functions visible to one invocation.
class function_table {
  public:
    // `override_ok` allows a later global source to replace an earlier one
    // (project over user); a duplicate inside one source is always an error, as
    // is colliding with a compiled step or a reserved name (`func`, `end`,
    // `prompt`).
    bool add(function_def def, bool override_ok, std::string& error);

    [[nodiscard]] function_def const* find(std::string_view name) const;
    [[nodiscard]] bool contains(std::string_view name) const;

    // Every definition visible to this run, in the order it was first added
    // (a project source replaces an earlier definition in place).
    [[nodiscard]] std::vector<function_def> const& all() const { return defs_; }

  private:
    std::vector<function_def> defs_;
};

// Load the global function files. `$PROMPT_FUNCS` (colon-separated), when set,
// replaces the default list; an empty value disables global functions. A
// missing file is fine, an unreadable or malformed one is an error.
bool load_function_files(function_table& table, std::filesystem::path const& git_root, std::string& error);

// Names and signatures of the `.func … .end` blocks in `tokens`, in order,
// skipping malformed ones (a bad signature, an unterminated block). Whole-line
// completion uses it to offer a function defined earlier on the same command
// line, or in an inlined `*.prompt` file, without expanding the chain.
std::vector<function_def> collect_inline_functions(std::span<std::string_view const> tokens);

// A one-line description of a function for completion, e.g. `function(task,
// ai=dseek)` or `function` when it takes no parameters.
std::string function_signature(function_def const& def);

// Expand `.func` definitions and `.name` calls in an argv (argv[0] is copied
// verbatim). `allow_chain` is false for a `funcs` file, where only `.func`
// blocks are legal. A non-empty `error` means print it and exit 2.
expanded_argv expand_functions(std::span<std::string const> argv, function_table& table, bool allow_chain);

} // namespace prompt
