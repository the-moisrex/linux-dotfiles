#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace prompt {

// Port of `_common.sh parse_arguments`' --head validation: finds "--head" in
// args, stores its value in head_lines and returns "" on success, or the exact
// message bash prints to stderr (the caller pairs it with exit code 2).
// head_lines is left untouched when --head is absent; a very large value is
// clamped (bash's `sed -n 1,Np` simply prints every line).
// Scripts with their own arg loop (note.sh, gtest-case.sh) accept any value —
// pass strict = false to mirror them (a non-numeric value becomes 0).
std::string parse_head_option(std::span<std::string_view const> args, std::size_t& head_lines,
                              bool strict = true) noexcept;

} // namespace prompt
