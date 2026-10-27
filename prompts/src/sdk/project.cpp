#include "project.hpp"
#include "../core/fs.hpp"
#include "../core/process.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::project {

namespace {

using std::filesystem::path;

bool is_file(path const& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

bool path_exists(path const& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

bool any_file(path const& dir, std::span<char const* const> names) {
    return std::ranges::any_of(names, [&](char const* name) { return is_file(dir / name); });
}

// The whole (small) file as text, or "" when missing/oversized.
std::string read_small(path const& p, std::uintmax_t limit = 1U << 20U) {
    std::error_code ec;
    if (std::filesystem::file_size(p, ec) > limit || ec) return {};
    std::ifstream file(p, std::ios::binary);
    if (!file) return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool file_contains(path const& p, std::string_view needle) { return read_small(p).contains(needle); }

// A PATH lookup without spawning a process (detection runs on every `.compress`
// and must stay cheap).
std::optional<path> find_on_path(std::string_view name) {
    char const* env = std::getenv("PATH");
    if (env == nullptr) return std::nullopt;
    std::string_view const all(env);
    std::size_t start = 0;
    while (start <= all.size()) {
        std::size_t end = all.find(':', start);
        std::string_view dir = all.substr(start, end == std::string_view::npos ? end : end - start);
        if (!dir.empty() && is_file(path{std::string(dir)} / name)) return path{std::string(dir)} / name;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return std::nullopt;
}

std::string run_git(path const& root, std::vector<std::string> const& args, std::chrono::milliseconds timeout) {
    std::vector<std::string> argv{"git", "-C", root.string()};
    argv.insert(argv.end(), args.begin(), args.end());
    auto result = process::command{argv}.capture_stderr().discard_stderr().timeout(timeout).run();
    return result.exit_code == 0 ? std::move(result.stdout_data) : std::string{};
}

std::string git_ls_files(path const& root) { return run_git(root, {"ls-files"}, std::chrono::seconds(10)); }

// `git grep -E -l <patterns> -- <globs>`: one command instead of reading the
// tree; empty when nothing matches (git grep exits 1).
bool git_grep(path const& root, std::vector<std::string> const& patterns, std::span<char const* const> globs = {}) {
    std::vector<std::string> argv{"grep", "-E", "-l", "--no-color"};
    for (auto const& pattern : patterns) {
        argv.emplace_back("-e");
        argv.push_back(pattern);
    }
    argv.emplace_back("--");
    if (globs.empty()) {
        argv.emplace_back(".");
    } else {
        for (char const* glob : globs) argv.emplace_back(glob);
    }
    return !run_git(root, argv, std::chrono::seconds(5)).empty();
}

std::string lower_ext(std::string_view extension) {
    std::string out;
    out.reserve(extension.size());
    for (char const c : extension) {
        out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

std::string extension_of(std::string_view line) {
    std::size_t slash = line.rfind('/');
    std::size_t dot = line.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
    return std::string(line.substr(dot));
}

void count_languages(std::string_view ls_files, std::map<language, std::size_t>& counts) {
    std::size_t start = 0;
    while (start < ls_files.size()) {
        std::size_t end = ls_files.find('\n', start);
        std::string_view line = ls_files.substr(start, end == std::string_view::npos ? end : end - start);
        if (!line.empty()) {
            language const lang = language_for_extension(extension_of(line));
            if (lang != language::unknown) ++counts[lang];
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
}

build_system detect_build(path const& root) {
    if (is_file(root / "CMakeLists.txt")) return build_system::cmake;
    if (is_file(root / "meson.build")) return build_system::meson;
    if (is_file(root / "BUILD.bazel") || is_file(root / "BUILD") || is_file(root / "WORKSPACE")) {
        return build_system::bazel;
    }
    if (is_file(root / "Cargo.toml")) return build_system::cargo;
    if (is_file(root / "go.mod")) return build_system::go_modules;
    if (is_file(root / "package.json")) return build_system::npm;
    if (is_file(root / "build.ninja") || is_file(root / ".ninja_deps")) return build_system::ninja;
    if (any_file(root, std::array<char const*, 3>{"Makefile", "makefile", "GNUmakefile"})) return build_system::make;
    if (any_file(root, std::array<char const*, 3>{"pyproject.toml", "setup.py", "requirements.txt"})) {
        return build_system::python;
    }
    return build_system::none;
}

package_manager detect_package(path const& root) {
    if (is_file(root / "Cargo.toml")) return package_manager::cargo;
    if (is_file(root / "go.mod")) return package_manager::go_modules;
    if (is_file(root / "yarn.lock")) return package_manager::yarn;
    if (is_file(root / "pnpm-lock.yaml")) return package_manager::pnpm;
    if (is_file(root / "bun.lockb") || is_file(root / "bun.lock")) return package_manager::bun;
    if (is_file(root / "package-lock.json")) return package_manager::npm;
    if (is_file(root / "poetry.lock")) return package_manager::poetry;
    if (is_file(root / "uv.lock")) return package_manager::uv;
    if (is_file(root / "Pipfile") || is_file(root / "Pipfile.lock")) return package_manager::pipenv;
    if (is_file(root / "package.json")) return package_manager::npm;
    if (any_file(root, std::array<char const*, 4>{"requirements.txt", "pyproject.toml", "setup.py", "setup.cfg"})) {
        return package_manager::pip;
    }
    if (is_file(root / "composer.json")) return package_manager::composer;
    if (is_file(root / "Gemfile")) return package_manager::bundler;

    // No project manifest: fall back to the host's package manager.
    struct host_rule {
        char const* program;
        package_manager manager;
    };
    static constexpr std::array hosts = {
        host_rule{.program = "pacman", .manager = package_manager::pacman},
        host_rule{.program = "apt-get", .manager = package_manager::apt},
        host_rule{.program = "dnf", .manager = package_manager::dnf},
        host_rule{.program = "apk", .manager = package_manager::apk},
        host_rule{.program = "brew", .manager = package_manager::brew},
    };
    for (auto const& rule : hosts) {
        if (find_on_path(rule.program)) return rule.manager;
    }
    return package_manager::unknown;
}

test_framework detect_tests(path const& root, build_system build, std::span<language const> languages) {
    bool const cpp = std::ranges::find(languages, language::cpp) != languages.end() ||
                     std::ranges::find(languages, language::c) != languages.end();
    if (cpp) {
        static constexpr std::array<char const*, 6> globs = {"*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hpp"};
        if (git_grep(root, {R"(^TEST(_F|_P)?\()"}, globs)) return test_framework::gtest;
        if (git_grep(root, {"catch2/catch", "CATCH_TEST_CASE"}, globs)) return test_framework::catch2;
        if (git_grep(root, {"doctest.h", "DOCTEST_TEST_CASE"}, globs)) return test_framework::doctest;
    }
    if (build == build_system::cargo) return test_framework::cargo_test;
    if (build == build_system::go_modules) return test_framework::go_test;

    if (is_file(root / "conftest.py") || is_file(root / "pytest.ini") || is_file(root / "tox.ini")) {
        return test_framework::pytest;
    }
    if (!run_git(root, {"grep", "-E", "-l", "--no-color", "-e", "^def test_", "--", "*.py"}, std::chrono::seconds(5))
             .empty()) {
        return test_framework::pytest;
    }
    if (file_contains(root / "package.json", "\"jest\"") || file_contains(root / "package.json", "\"vitest\"")) {
        return test_framework::jest;
    }
    return test_framework::unknown;
}

std::optional<path> detect_build_dir(path const& root, bool& compile_commands) {
    static constexpr std::array<char const*, 10> dirs = {
        "build",
        "build-dev",
        "build-dev-clang",
        "build-dev-gcc",
        "cmake-build-debug",
        "cmake-build-release",
        "out",
        "build/Debug",
        "build/Release",
        ".",
    };
    std::vector<path> bases{root};
    path const cwd = std::filesystem::current_path();
    if (cwd != root) bases.push_back(cwd);

    for (auto const& base : bases) {
        for (char const* dir : dirs) {
            path candidate = base / dir;
            if (is_file(candidate / "compile_commands.json")) compile_commands = true;
            if (is_file(candidate / "CMakeCache.txt") || is_file(candidate / "build.ninja") ||
                is_file(candidate / "compile_commands.json") || is_file(candidate / "Makefile")) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

} // namespace

std::string_view to_string(language value) {
    switch (value) {
    case language::c:
        return "c";
    case language::cpp:
        return "cpp";
    case language::python:
        return "python";
    case language::javascript:
        return "javascript";
    case language::typescript:
        return "typescript";
    case language::rust:
        return "rust";
    case language::go:
        return "go";
    case language::shell:
        return "shell";
    case language::java:
        return "java";
    case language::kotlin:
        return "kotlin";
    case language::ruby:
        return "ruby";
    case language::php:
        return "php";
    case language::csharp:
        return "csharp";
    case language::unknown:
        break;
    }
    return "unknown";
}

std::string_view to_string(build_system value) {
    switch (value) {
    case build_system::cmake:
        return "cmake";
    case build_system::ninja:
        return "ninja";
    case build_system::make:
        return "make";
    case build_system::meson:
        return "meson";
    case build_system::bazel:
        return "bazel";
    case build_system::cargo:
        return "cargo";
    case build_system::npm:
        return "npm";
    case build_system::go_modules:
        return "go";
    case build_system::python:
        return "python";
    case build_system::none:
        break;
    }
    return "none";
}

std::string_view to_string(package_manager value) {
    switch (value) {
    case package_manager::npm:
        return "npm";
    case package_manager::yarn:
        return "yarn";
    case package_manager::pnpm:
        return "pnpm";
    case package_manager::bun:
        return "bun";
    case package_manager::pip:
        return "pip";
    case package_manager::pipenv:
        return "pipenv";
    case package_manager::poetry:
        return "poetry";
    case package_manager::uv:
        return "uv";
    case package_manager::conda:
        return "conda";
    case package_manager::cargo:
        return "cargo";
    case package_manager::go_modules:
        return "go modules";
    case package_manager::composer:
        return "composer";
    case package_manager::bundler:
        return "bundler";
    case package_manager::pacman:
        return "pacman";
    case package_manager::apt:
        return "apt";
    case package_manager::dnf:
        return "dnf";
    case package_manager::apk:
        return "apk";
    case package_manager::brew:
        return "brew";
    case package_manager::unknown:
        break;
    }
    return "unknown";
}

std::string_view to_string(test_framework value) {
    switch (value) {
    case test_framework::gtest:
        return "gtest";
    case test_framework::catch2:
        return "catch2";
    case test_framework::doctest:
        return "doctest";
    case test_framework::boost_test:
        return "boost-test";
    case test_framework::pytest:
        return "pytest";
    case test_framework::jest:
        return "jest";
    case test_framework::cargo_test:
        return "cargo-test";
    case test_framework::go_test:
        return "go-test";
    case test_framework::unknown:
        break;
    }
    return "unknown";
}

language language_for_extension(std::string_view ext) {
    std::string const e = lower_ext(ext.starts_with('.') ? ext.substr(1) : ext);
    if (e == "c") return language::c;
    if (e == "h") return language::c;
    if (e == "cc" || e == "cpp" || e == "cxx" || e == "c++" || e == "hpp" || e == "hxx" || e == "hh" || e == "ipp") {
        return language::cpp;
    }
    if (e == "py" || e == "pyi") return language::python;
    if (e == "js" || e == "mjs" || e == "cjs" || e == "jsx") return language::javascript;
    if (e == "ts" || e == "tsx" || e == "mts") return language::typescript;
    if (e == "rs") return language::rust;
    if (e == "go") return language::go;
    if (e == "sh" || e == "bash" || e == "zsh" || e == "fish") return language::shell;
    if (e == "java") return language::java;
    if (e == "kt" || e == "kts") return language::kotlin;
    if (e == "rb") return language::ruby;
    if (e == "php") return language::php;
    if (e == "cs") return language::csharp;
    return language::unknown;
}

language language_for_path(std::string_view path_text) { return language_for_extension(extension_of(path_text)); }

info detect(path const& requested_root, environment const& env) {
    (void)env;
    path root = requested_root.empty() ? std::filesystem::current_path() : requested_root;
    {
        std::error_code ec;
        root = std::filesystem::weakly_canonical(root, ec);
        if (ec) root = requested_root.empty() ? std::filesystem::current_path() : requested_root;
    }

    static std::optional<info> cache;
    if (cache && cache->root == root) return *cache;

    info out;
    out.root = root;

    if (path_exists(root / ".git")) {
        std::map<language, std::size_t> counts;
        count_languages(git_ls_files(root), counts);
        std::vector<std::pair<language, std::size_t>> ranked(counts.begin(), counts.end());
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](auto const& a, auto const& b) { return a.second > b.second; });
        for (auto const& [lang, count] : ranked) {
            (void)count;
            out.languages.push_back(lang);
        }
    }

    out.build = detect_build(root);
    out.package = detect_package(root);
    out.tests = detect_tests(root, out.build, out.languages);
    bool compile_commands = false;
    out.build_dir = detect_build_dir(root, compile_commands);
    out.tools.compile_commands = compile_commands;
    out.tools.universal_ctags = find_on_path("ctags").has_value();

    cache = out;
    return out;
}

std::string summary_line(info const& value) {
    std::string langs;
    for (auto const lang : value.languages) {
        if (!langs.empty()) langs += ", ";
        langs += std::string(to_string(lang));
        if (langs.size() > 60) {
            langs += ", …";
            break;
        }
    }
    if (langs.empty()) langs = "unknown";

    std::string out = langs + " · build: " + std::string(to_string(value.build));
    if (value.build_dir) out += " (" + value.build_dir->string() + ")";
    out += std::string(" · pkg: ") + std::string(to_string(value.package));
    out += std::string(" · tests: ") + std::string(to_string(value.tests));
    if (value.tools.universal_ctags) out += " · ctags";
    if (value.tools.compile_commands) out += " · clangd";
    return out;
}

} // namespace prompt::project
