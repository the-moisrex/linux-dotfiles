#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace prompt::fs {

std::optional<std::filesystem::path>
find_git_root(std::filesystem::path const& start = std::filesystem::current_path()) noexcept;

std::optional<std::filesystem::path> self_path() noexcept;

std::filesystem::path relative_path(std::filesystem::path const& file,
                                    std::optional<std::filesystem::path> const& git_root) noexcept;

// Resolve a repository utility (bin/run, bin/tse, bin/gtest-case, ...) without
// going through prompts/. Search order:
//   1. exe_path/bin/<name>          (installed layout: prompt lives in bin/)
//   2. exe_path/../../bin/<name>    (build layout: .../cpp_prompt/build/prompt)
//   3. git_root/bin/<name>          (running from anywhere in the repo)
//   4. walk up from cwd to <dir>/bin/<name>
//   5. bare <name> (PATH lookup by the shell)
std::filesystem::path bin_tool(std::string_view name, std::filesystem::path const& exe_path,
                               std::optional<std::filesystem::path> const& git_root) noexcept;

} // namespace prompt::fs