#pragma once

#include "stage.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::project {

// The shape of a project, as enums rather than free-form strings: a compressor
// branches on `build == build_system::cmake`, not on a string compare, and
// `.project` prints the canonical name.
enum class language : std::uint8_t {
    unknown,
    c,
    cpp,
    python,
    javascript,
    typescript,
    rust,
    go,
    shell,
    java,
    kotlin,
    ruby,
    php,
    csharp,
};

enum class build_system : std::uint8_t {
    none,
    cmake,
    ninja,
    make,
    meson,
    bazel,
    cargo,
    npm,
    go_modules,
    python,
};

enum class package_manager : std::uint8_t {
    unknown,
    npm,
    yarn,
    pnpm,
    bun,
    pip,
    pipenv,
    poetry,
    uv,
    conda,
    cargo,
    go_modules,
    composer,
    bundler,
    pacman,
    apt,
    dnf,
    apk,
    brew,
};

enum class test_framework : std::uint8_t {
    unknown,
    gtest,
    catch2,
    doctest,
    boost_test,
    pytest,
    jest,
    cargo_test,
    go_test,
};

[[nodiscard]] std::string_view to_string(language value);
[[nodiscard]] std::string_view to_string(build_system value);
[[nodiscard]] std::string_view to_string(package_manager value);
[[nodiscard]] std::string_view to_string(test_framework value);

// Which compression-relevant tools exist for this project.
struct tooling {
    bool universal_ctags = false;  // accurate C/C++ symbol listing
    bool compile_commands = false; // clangd/clang-tidy usable
};

struct info {
    std::filesystem::path root;
    std::vector<language> languages; // descending by tracked-file count
    build_system build = build_system::none;
    package_manager package = package_manager::unknown;
    test_framework tests = test_framework::unknown;
    std::optional<std::filesystem::path> build_dir; // holds the build files
    tooling tools;
};

// Detect the project rooted at `root` (the git root when the caller has one).
// Cheap: a handful of stat() calls, one `git ls-files` when the root is a
// repository, and a single `git grep` when C/C++ files are present. Cached per
// process for the requested root, so `.project` and `.compress` share one scan.
[[nodiscard]] info detect(std::filesystem::path const& root, environment const& env);

// The language a path's extension maps to (`unknown` for anything else). Shared
// with the language histogram so the two never disagree.
[[nodiscard]] language language_for_path(std::string_view path);
[[nodiscard]] language language_for_extension(std::string_view ext);

// A compact one-or-two-line summary, for `.project` and the compression legend.
[[nodiscard]] std::string summary_line(info const& value);

} // namespace prompt::project
