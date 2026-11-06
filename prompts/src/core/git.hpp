#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::git {

// Run a git command and return stdout with trailing newlines stripped.
std::string run(std::vector<std::string> const& args);

std::string run(std::initializer_list<char const*> args);

// Annotate ordinary unified diffs using destination-file line numbers.
// Combined diffs (multiple prefix columns) are left unchanged.
std::string add_line_numbers_to_diff(std::string_view diff);

// Keep/drop whole-file blocks of a unified diff.
//   * `includes` non-empty => only blocks whose path matches one of them are kept.
//   * `excludes`          => blocks whose path matches any of them are dropped.
// Paths are matched both verbatim and by basename, using `detail::glob_match`.
std::string filter_diff_by_path(std::string_view diff, std::vector<std::string> const& includes,
                                std::vector<std::string> const& excludes);
} // namespace prompt::git
