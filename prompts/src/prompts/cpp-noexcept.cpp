#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <format>
#include <string>
#include <string_view>

namespace prompt::prompts {

namespace {

constexpr std::string_view kSummary = "Rewrite this C++ code to be truly noexcept where it reasonably can be.";

} // namespace

static void execute_cpp_noexcept(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Make the following C++ code truly noexcept wherever it reasonably can be.

For each function, judge whether it can honestly promise not to throw. When it
can, rewrite it so it no longer has any throw site:

- Remove allocations entirely where you can: use std::array or fixed-capacity
  buffers instead of std::vector/std::string, keep data on the stack, pass
  scratch space in from the caller.
- A successful reserve() prevents later growth but can itself throw. Do not
  call it inside a noexcept function unless failure is caught and reported.
  Prefer caller-owned storage or a non-throwing allocation protocol that
  checks failure and returns a status to the caller.
- Replace library calls that may throw (throwing string/vector growth,
  std::stoi/std::stol, container operations that may reallocate, filesystem
  calls that throw, format/printf-style APIs that allocate) with their
  non-throwing counterpart, or check preconditions before the call and return
  an error code.
- Make as much of each function as possible pure: no allocations, no I/O, no
  global mutable state, no virtual calls that could throw, no locks that could
  throw, no exceptions from callbacks. Small pure helpers are the building
  blocks of noexcept functions.
- Keep any remaining throw site honest: if a function genuinely must throw,
  leave it alone and add a one-line comment explaining why (e.g. "may throw
  std::bad_alloc from the string copy").

Return the answer primarily as a git diff that can be applied directly. In each
touched function, annotate the declaration with `noexcept` where you justified
it. For every function you change, add a short bullet saying:
  - why it is now safely noexcept, and
  - what throw site was removed or made non-throwing.

Prefer minimal, surgical changes: do not restructure unrelated code, do not
touch functions that already are correctly noexcept, and do not weaken error
reporting — a failure must still be visible to the caller (return value, status
enum, assert, or a documented precondition).

)PROMPT";

    std::string error;
    auto sources = collect_file_args(v);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    if (v.in.piped()) {
        output += embed_stdin(v.in.text(), opts.head_lines);
        v.in.spend();
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_cpp_noexcept(std::ostream& os) {
    os << file_usage_help("cpp.noexcept") << "\n"
       << kSummary << R"EOF(

For each function that could reasonably become noexcept, find a way to avoid
throwing — eliminate allocations where you can, make the remaining ones
non-throwing, and replace throw-capable library calls with their
status-returning equivalents. Functions that genuinely must throw are left
alone with a one-line note explaining why.

Return the answer primarily as a git diff.

Options:
)EOF" << file_options_help("the embedded C++ code");
}

bool const registered =
    prompt::register_prompt("cpp.noexcept", kSummary, &execute_cpp_noexcept, &render_help_cpp_noexcept);

} // namespace prompt::prompts
