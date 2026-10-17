#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace prompt::fs {

std::optional<std::filesystem::path>
find_git_root(std::filesystem::path const& start = std::filesystem::current_path());

std::optional<std::filesystem::path> self_path();

std::filesystem::path relative_path(std::filesystem::path const& file,
                                    std::optional<std::filesystem::path> const& git_root);

// Resolve a repository utility (bin/run, bin/tse, bin/gtest-case, ...) without
// going through prompts/. Search order:
//   1. exe_path's directory/<name>   (installed layout: prompt sits in bin/)
//   2. exe_path/../../bin/<name>     (build layout: .../prompts/build/prompt)
//   3. git_root/bin/<name>           (running from anywhere in the repo)
//   4. walk up from cwd to <dir>/bin/<name>
//   5. $PATH/<name>, as an absolute path (so `python3 <tool>` still works)
//   6. bare <name> (last resort: PATH lookup by the shell)
std::filesystem::path bin_tool(std::string_view name, std::filesystem::path const& exe_path,
                               std::optional<std::filesystem::path> const& git_root);

} // namespace prompt::fs