#pragma once

#include "embed.hpp"
#include "prompt_context.hpp"
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// --head validation: finds "--head" in args, stores its value in head_lines and
// returns "" on success, or the exact message to print on stderr (the caller
// pairs it with exit code 2).
// head_lines stays nullopt when --head is absent — unset stays distinct from
// `--head 0`, and `sed`'s `1,0p` prints line 1 — and a very large value is
// clamped (it simply prints every line).
// Prompts with their own arg loop (note, gtest-case) accept any value — pass
// strict = false to mirror them (a non-numeric value becomes 0).
std::string parse_head_option(std::span<std::string_view const> args, std::optional<std::size_t>& head_lines,
                              bool strict = true);

// parse_head_option plus the `throw usage_error{...}` that opens most prompts
// (the three-line preamble those prompts repeat). Returns the cap, or throws
// the exact message parse_head_option produced. `strict = false` mirrors the
// scripts with their own arg loop.
std::optional<std::size_t> head_lines_or_throw(prompt_context const& ctx, bool strict = true);

// The value-less flags that ask the file prompts to number the embedded lines.
inline constexpr std::array<std::string_view, 2> kNumberFlags = {"--num", "--numbered"};

// True when `arg` is one of kNumberFlags. The prompts that walk their own
// arguments (git-dirty, overengineering, paths, skill) call this instead of
// open-coding the comparison, so adding another spelling only touches
// kNumberFlags.
bool is_number_flag(std::string_view arg);

// The file prompts' whole CLI: `--head N` (validated like parse_head_option)
// plus the value-less `--num`/`--numbered`. Throws usage_error on a bad --head
// value, exactly like head_lines_or_throw.
file_options file_options_or_throw(prompt_context const& ctx, bool strict = true);

// The two usage lines shared by the file prompts:
//   Usage: prompt <name> <prefix>[--head N] [--num]<tail>
//          <pipe> | prompt <name> <prefix>[--head N] [--num]<tail>
// `prefix` goes before the flags (e.g. "<name>" for skill), `tail` after them
// (default "[FILE...]"; pass "" for the prompts that take no files).
std::string file_usage_help(std::string_view name, std::string_view tail = "[FILE...]",
                            std::string_view pipe = "some-command", std::string_view prefix = {});

// The `--head`/`--num` lines of a file prompt's `Options:` block. `what` names
// the content --head caps (e.g. "the embedded context"); `show_help` appends
// the standard `-h, --help` line.
std::string file_options_help(std::string_view what, bool show_help = false);

// The context's arguments with the `--head <value>` pair and the value-less
// --num/--numbered removed: the filter loop the prompts run before treating
// each remaining token as a path. The strings are owned copies (callers build
// argv vectors and concatenate them).
std::vector<std::string> positional_args(prompt_context const& ctx);

// The FILE arguments (everything but --head's value and --num/--numbered) split
// into the ones that are regular files and the ones that are not. Prompts embed
// the former and warn about the latter; stdout and stderr are independent
// streams, so visiting the two lists in separate passes keeps every byte the
// bash scripts produce.
struct file_args {
    std::vector<std::filesystem::path> files;
    std::vector<std::string_view> missing;
};

file_args collect_file_args(prompt_context const& ctx);

} // namespace prompt
