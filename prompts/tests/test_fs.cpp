#include "../src/core/fs.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using prompt::fs::bin_tool;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void make_tool(fs::path const& p) {
    std::FILE* f = std::fopen(p.c_str(), "w");
    if (f != nullptr) {
        std::fputs("#!/bin/sh\nexit 0\n", f);
        std::fclose(f);
    }
    fs::permissions(p, fs::perms::owner_all, fs::perm_options::replace);
}

// Regression: with the binary installed as <prefix>/bin/prompt, its tools are
// siblings in bin/ — not under <prefix>/bin/bin/. The old code missed them and
// fell back to a bare name, which callers then opened with `python3 <tool>`
// relative to the cwd (e.g. `python3: can't open file '.../tse'`).
void test_installed_layout(fs::path const& root) {
    auto bin = root / "installed" / "bin";
    fs::create_directories(bin);
    auto tool = bin / "prompttesttool";
    make_tool(tool);

    auto got = bin_tool("prompttesttool", bin / "prompt", std::nullopt);
    check(got == tool, "installed layout: tool is a sibling of the binary");
}

void test_build_layout(fs::path const& root) {
    auto exe_dir = root / "buildrepo" / "prompts" / "build";
    fs::create_directories(exe_dir);
    auto bin = root / "buildrepo" / "bin";
    fs::create_directories(bin);
    auto tool = bin / "prompttesttool";
    make_tool(tool);

    auto got = bin_tool("prompttesttool", exe_dir / "prompt", std::nullopt);
    check(got == tool, "build layout: exe_dir/../../bin is searched");
}

void test_git_root(fs::path const& root) {
    auto exe_dir = root / "elsewhere";
    fs::create_directories(exe_dir);
    auto git_root = root / "gitrepo";
    fs::create_directories(git_root / "bin");
    auto tool = git_root / "bin" / "prompttesttool";
    make_tool(tool);

    auto got = bin_tool("prompttesttool", exe_dir / "prompt", git_root);
    check(got == tool, "git_root/bin is searched");
}

void test_path_fallback(fs::path const& root) {
    // No bin/ dir near the executable or the cwd: the PATH entry must come
    // back absolute, so `python3 <tool>` opens it instead of looking in cwd.
    auto path_dir = root / "pathdir";
    fs::create_directories(path_dir);
    auto tool = path_dir / "prompttesttool";
    make_tool(tool);

    auto exe_dir = root / "nobindir";
    fs::create_directories(exe_dir);

    std::string const saved = std::getenv("PATH") != nullptr ? std::getenv("PATH") : "";
    std::string const path = path_dir.string();
    ::setenv("PATH", path.c_str(), 1);
    auto got = bin_tool("prompttesttool", exe_dir / "prompt", std::nullopt);
    ::setenv("PATH", saved.c_str(), 1);

    check(got.is_absolute(), "path fallback: result is absolute");
    check(got == tool, "path fallback: resolves through PATH");
}

void test_missing_returns_bare() {
    auto got = bin_tool("prompttesttool-missing", {}, std::nullopt);
    check(got == fs::path("prompttesttool-missing"), "missing tool: falls back to the bare name");
}

} // namespace

int main() {
    auto root = fs::temp_directory_path() / ("prompt-test-fs-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    test_installed_layout(root);
    test_build_layout(root);
    test_git_root(root);
    test_path_fallback(root);
    test_missing_returns_bare();

    fs::remove_all(root, ec);

    if (failures == 0) {
        std::printf("all fs tests passed\n");
        return 0;
    }
    return 1;
}
