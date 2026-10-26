#include "../src/sdk/prompt_file.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unistd.h>
#include <vector>

using namespace prompt;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

std::filesystem::path scratch_dir() {
    auto dir = std::filesystem::temp_directory_path() / ("prompt-file-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void write_file(std::filesystem::path const& path, std::string const& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

expanded_argv expand(std::vector<std::string> const& argv, std::filesystem::path const& git_root = {},
                     bool expand_files = true) {
    std::vector<char const*> raw;
    raw.reserve(argv.size());
    for (auto const& arg : argv) raw.push_back(arg.c_str());
    return expand_prompt_files(std::span<char const* const>(raw.data(), raw.size()), git_root, expand_files);
}

bool same(std::vector<std::string> const& a, std::vector<std::string> const& b) { return a == b; }

void test_suffix() {
    check(has_prompt_file_suffix("debug.prompt"), "suffix: a bare name matches");
    check(has_prompt_file_suffix("sub/x.prompt"), "suffix: a path matches");
    check(!has_prompt_file_suffix(".prompt"), "suffix: the bare extension does not match");
    check(!has_prompt_file_suffix("prompt"), "suffix: a plain word does not match");
    check(!has_prompt_file_suffix("x.prompt.txt"), "suffix: the extension must be last");
}

void test_tokens(std::filesystem::path const& dir) {
    auto const file = dir / "tokens.prompt";
    write_file(file, "# leading comment\n"
                     ".note \"hello world\"   # trailing comment\n"
                     ".files  a.cpp  b.cpp\n"
                     ".cli 'single quoted # stays'\n"
                     "x#y\n"
                     "\"a\\\"b\" 'c'\n"
                     "back\\ slash\n"
                     "\n");
    auto const result = expand({"prompt", file.string()});
    check(result.error.empty(), "tokens: no error");
    check(same(result.storage,
               {
                   "prompt",
                   ".note",
                   "hello world",
                   ".files",
                   "a.cpp",
                   "b.cpp",
                   ".cli",
                   "single quoted # stays",
                   "x#y",
                   "a\"b",
                   "c",
                   "back slash",
               }),
          "tokens: comments, quotes and escapes");
}

void test_unterminated_quote(std::filesystem::path const& dir) {
    auto const file = dir / "bad.prompt";
    write_file(file, ".note ok\n.note \"oops\n");
    auto const result = expand({"prompt", file.string()});
    check(!result.error.empty(), "unterminated: reported");
    check(result.error.contains(":2: unterminated \""), "unterminated: line and quote named");
    check(result.error.starts_with("prompt: "), "unterminated: prompt: prefix");
}

void test_inline_and_order(std::filesystem::path const& dir) {
    auto const file = dir / "chain.prompt";
    write_file(file, ".note hi\n");
    auto const result = expand({"prompt", "--head", "10", file.string(), ".length", "short"});
    check(result.error.empty(), "inline: no error");
    check(same(result.storage, {"prompt", "--head", "10", ".note", "hi", ".length", "short"}),
          "inline: replaced in place, later tokens kept");
}

void test_leading_prompt_dropped(std::filesystem::path const& dir) {
    auto const file = dir / "leading.prompt";
    write_file(file, "prompt .note hi\n");
    auto const result = expand({"prompt", file.string()});
    check(same(result.storage, {"prompt", ".note", "hi"}), "leading prompt: dropped");
}

void test_verbatim_tail(std::filesystem::path const& dir) {
    auto const file = dir / "tail.prompt";
    write_file(file, ".exec -- run x.prompt\n");
    auto const result = expand({"prompt", file.string()});
    check(result.error.empty(), "verbatim: a missing file after -- is not an error");
    check(same(result.storage, {"prompt", ".exec", "--", "run", "x.prompt"}),
          "verbatim: everything after -- is untouched");

    auto const outer = expand({"prompt", "--", "x.prompt"});
    check(same(outer.storage, {"prompt", "--", "x.prompt"}), "verbatim: -- in argv stops the pass");
}

void test_head_position_error(std::filesystem::path const& dir) {
    auto const result = expand({"prompt", (dir / "missing.prompt").string()});
    check(!result.error.empty(), "head: a missing file is an error");
    check(result.error.contains("cannot find prompt file"), "head: names the missing file");

    // Elsewhere the prompt owning the argument decides, so `.files` keeps its
    // own warning instead of aborting the chain.
    auto const argument = expand({"prompt", ".files", (dir / "missing.prompt").string()});
    check(argument.error.empty(), "argument: a missing file is not an error");
    check(same(argument.storage, {"prompt", ".files", (dir / "missing.prompt").string()}),
          "argument: the token is passed through");
}

void test_nested(std::filesystem::path const& dir) {
    write_file(dir / "outer.prompt", "inner.prompt\n.length short\n");
    write_file(dir / "inner.prompt", ".note nested\n");
    auto const result = expand({"prompt", (dir / "outer.prompt").string()});
    check(result.error.empty(), "nested: no error");
    check(same(result.storage, {"prompt", ".note", "nested", ".length", "short"}), "nested: spliced recursively");
}

void test_cycle(std::filesystem::path const& dir) {
    write_file(dir / "a.prompt", "b.prompt\n");
    write_file(dir / "b.prompt", "a.prompt\n");
    auto const result = expand({"prompt", (dir / "a.prompt").string()});
    check(!result.error.empty(), "cycle: reported");
    check(result.error.contains("prompt file cycle"), "cycle: named");
}

void test_depth(std::filesystem::path const& dir) {
    // A chain of kMaxPromptFileDepth files resolves; one more does not.
    auto const last = kMaxPromptFileDepth - 1;
    for (std::size_t i = 0; i < last; ++i)
        write_file(dir / ("deep" + std::to_string(i) + ".prompt"), "deep" + std::to_string(i + 1) + ".prompt\n");
    write_file(dir / ("deep" + std::to_string(last) + ".prompt"), ".note bottom\n");

    auto const ok = expand({"prompt", (dir / "deep0.prompt").string()});
    check(ok.error.empty(), "depth: the limit itself is allowed");
    check(ok.storage.size() > 2 && ok.storage.back() == "bottom", "depth: the bottom file is spliced");

    write_file(dir / ("deep" + std::to_string(last) + ".prompt"), "deeper.prompt\n");
    write_file(dir / "deeper.prompt", ".note too deep\n");
    auto const too_deep = expand({"prompt", (dir / "deep0.prompt").string()});
    check(!too_deep.error.empty(), "depth: one level too many is an error");
    check(too_deep.error.contains("nested too deep"), "depth: named");
}

void test_git_root_lookup(std::filesystem::path const& dir) {
    auto const root = dir / "root";
    write_file(root / ".prompt" / "shared.prompt", ".note from-root\n");
    auto const result = expand({"prompt", "shared.prompt"}, root);
    check(result.error.empty(), "git root: resolved");
    check(same(result.storage, {"prompt", ".note", "from-root"}), "git root: spliced");
}

void test_config_lookup(std::filesystem::path const& dir) {
    auto const config = dir / "config";
    write_file(config / "prompt" / "prompts" / "personal.prompt", ".note from-config\n");
    ::setenv("XDG_CONFIG_HOME", config.c_str(), 1);
    auto const result = expand({"prompt", "personal.prompt"});
    ::unsetenv("XDG_CONFIG_HOME");
    check(result.error.empty(), "config dir: resolved");
    check(same(result.storage, {"prompt", ".note", "from-config"}), "config dir: spliced");
}

void test_disabled_and_empty(std::filesystem::path const& dir) {
    auto const file = dir / "off.prompt";
    write_file(file, ".note hi\n");
    auto const off = expand({"prompt", file.string()}, {}, false);
    check(off.error.empty(), "disabled: no error");
    check(same(off.storage, {"prompt", file.string()}), "disabled: argv is copied unchanged");

    write_file(dir / "empty.prompt", "# nothing but a comment\n\n");
    auto const empty = expand({"prompt", (dir / "empty.prompt").string()});
    check(empty.error.empty(), "empty: no error");
    check(same(empty.storage, {"prompt"}), "empty: expands to nothing");
}

} // namespace

int main() {
    auto const dir = scratch_dir();
    test_suffix();
    test_tokens(dir);
    test_unterminated_quote(dir);
    test_inline_and_order(dir);
    test_leading_prompt_dropped(dir);
    test_verbatim_tail(dir);
    test_head_position_error(dir);
    test_nested(dir);
    test_cycle(dir);
    test_depth(dir);
    test_git_root_lookup(dir);
    test_config_lookup(dir);
    test_disabled_and_empty(dir);
    std::filesystem::remove_all(dir);

    if (failures != 0) std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
