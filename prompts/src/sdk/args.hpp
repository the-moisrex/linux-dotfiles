#pragma once

#include "prompt_context.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// Port of `_common.sh parse_arguments`' --head validation: finds "--head" in
// args, stores its value in head_lines and returns "" on success, or the exact
// message bash prints to stderr (the caller pairs it with exit code 2).
// head_lines stays nullopt when --head is absent -- bash keeps head_lines=""
// (unset) distinct from head_lines="0", and sed's `1,0p` prints line 1 -- and a
// very large value is clamped (bash's `sed -n 1,Np` simply prints every line).
// Scripts with their own arg loop (note.sh, gtest-case.sh) accept any value —
// pass strict = false to mirror them (a non-numeric value becomes 0).
std::string parse_head_option(std::span<std::string_view const> args, std::optional<std::size_t>& head_lines,
                              bool strict = true);

// parse_head_option plus the `throw usage_error{...}` that opens most prompts
// (the three-line preamble those prompts repeat). Returns the cap, or throws
// the exact message parse_head_option produced. `strict = false` mirrors the
// scripts with their own arg loop.
std::optional<std::size_t> head_lines_or_throw(prompt_context const& ctx, bool strict = true);

// The context's arguments with the `--head <value>` pair removed: the filter
// loop file prompts run before treating each remaining token as a path. The
// strings are owned copies (callers build argv vectors and concatenate them).
std::vector<std::string> positional_args(prompt_context const& ctx);

// The FILE arguments (everything but the value of --head) split into the ones
// that are regular files and the ones that are not. Prompts embed the former and
// warn about the latter; stdout and stderr are independent streams, so visiting
// the two lists in separate passes keeps every byte the bash scripts produce.
struct file_args {
    std::vector<std::filesystem::path> files;
    std::vector<std::string_view> missing;
};

file_args collect_file_args(prompt_context const& ctx);

} // namespace prompt
