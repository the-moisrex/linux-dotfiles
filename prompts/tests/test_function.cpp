#include "../src/sdk/function.hpp"
#include <array>
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

expanded_argv expand(std::vector<std::string> const& argv, bool allow_chain = true) {
    function_table table;
    return expand_functions(argv, table, allow_chain);
}

bool same(std::vector<std::string> const& a, std::vector<std::string> const& b) { return a == b; }

std::filesystem::path scratch_dir() {
    auto dir = std::filesystem::temp_directory_path() / ("prompt-func-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void write_file(std::filesystem::path const& path, std::string const& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

void test_definition_and_call() {
    auto const r = expand({"prompt", ".func", "hi", ".note", "hello", ".end", ".hi"});
    check(r.error.empty(), "call: no error");
    check(same(r.storage, {"prompt", ".note", "hello"}), "call: definition removed, body spliced");
}

void test_params() {
    auto const positional =
        expand({"prompt", ".func", "greet(who,ai=dseek)", ".note", "hi {who} {ai}", ".end", ".greet", "world"});
    check(positional.error.empty(), "param: no error");
    check(same(positional.storage, {"prompt", ".note", "hi world dseek"}), "param: positional + default");

    auto const named = expand(
        {"prompt", ".func", "greet(who,ai=dseek)", ".note", "hi {who} {ai}", ".end", ".greet", "--ai", "grok", "bob"});
    check(named.error.empty(), "param: named flag binds by name, positional fills the rest");
    check(same(named.storage, {"prompt", ".note", "hi bob grok"}), "param: named + positional");
}

void test_specials() {
    auto const spread = expand({"prompt", ".func", "spread(a)", ".files", "{@}", ".end", ".spread", "x", "y", "z"});
    check(spread.error.empty(), "specials: no error");
    check(same(spread.storage, {"prompt", ".files", "y", "z"}), "specials: {@} is the extra positionals");

    auto const indexed = expand({"prompt", ".func", "f(a,b)", ".note", "{0}:{2}-{1}", ".end", ".f", "one", "two"});
    check(same(indexed.storage, {"prompt", ".note", "f:two-one"}), "specials: {0} is the name, {N} the Nth argument");

    auto const embedded = expand({"prompt", ".func", "f(ai=grok)", ".ai.{ai}", ".end", ".f"});
    check(same(embedded.storage, {"prompt", ".ai.grok"}), "specials: a placeholder substitutes inside a token");
}

void test_unknown_placeholder_kept() {
    // A `{field}` that is not a parameter belongs to `.each`, so it survives.
    auto const r = expand({"prompt", ".func", "loop(sym)", ".note", "{sym} {symbol}", ".end", ".loop", "a"});
    check(r.error.empty(), "each: no error");
    check(same(r.storage, {"prompt", ".note", "a {symbol}"}), "each: unknown placeholder is left alone");
}

void test_argument_stops_at_step() {
    auto const r = expand({"prompt", ".func", "f(a)", ".note", "{a}", ".end", ".f", "x", ".length", "short"});
    check(r.error.empty(), "stop: no error");
    check(same(r.storage, {"prompt", ".note", "x", ".length", "short"}), "stop: the next step is not an argument");
}

void test_body_composition() {
    auto const each = expand({"prompt", ".func", "loop", ".each", ".length", "5", ".end", ".end", ".loop"});
    check(each.error.empty(), "each body: no error");
    check(same(each.storage, {"prompt", ".each", ".length", "5", ".end"}), "each body: the `.each` body survives");

    auto const nested =
        expand({"prompt", ".func", "a", ".note", "a", ".end", ".func", "b", ".a", ".note", "b", ".end", ".b"});
    check(nested.error.empty(), "nested: no error");
    check(same(nested.storage, {"prompt", ".note", "a", ".note", "b"}), "nested: one function calls another");

    auto const verbatim = expand({"prompt", ".func", "a", ".exec", "--", "x", ".end", ".a"});
    check(same(verbatim.storage, {"prompt", ".exec", "--", "x"}), "verbatim: `--` tail is copied untouched");
}

void test_errors() {
    check(!expand({"prompt", ".func", "f(a)", ".note", "{a}", ".end", ".f"}).error.empty(),
          "missing: a required argument is an error");
    check(!expand({"prompt", ".func", "f(a)", ".note", "{a}", ".end", ".f", "--b", "1"}).error.empty(),
          "unknown: an undeclared --param is an error");
    check(!expand({"prompt", ".func", "note", ".note", "x", ".end"}).error.empty(),
          "shadow: a built-in name is an error");
    check(!expand({"prompt", ".func", "end", ".note", "x", ".end"}).error.empty(), "reserved: `end` is an error");
    check(!expand({"prompt", ".func", "f", ".note", "a", ".end", ".func", "f", ".note", "b", ".end"}).error.empty(),
          "duplicate: two definitions in one place are an error");
    check(!expand({"prompt", ".func", "f", ".note", "x"}).error.empty(), "unterminated: a missing .end is an error");
    check(!expand({"prompt", ".func", "r", ".r", ".end", ".r"}).error.empty(), "recursion: a cycle is an error");
    check(!expand({"prompt", ".note", "x"}, /*allow_chain=*/false).error.empty(),
          "file: a non-definition token is an error in a funcs file");
}

void test_passthrough() {
    auto const r = expand({"prompt", ".note", "hi", ".length", "short", ".unknown", "x"});
    check(r.error.empty(), "passthrough: no error");
    check(same(r.storage, {"prompt", ".note", "hi", ".length", "short", ".unknown", "x"}),
          "passthrough: ordinary prompts and unknown tokens are unchanged");
}

void test_global_files(std::filesystem::path const& dir) {
    auto const shared = dir / "funcs";
    write_file(shared, "# a comment\n"
                       ".func global(who)\n"
                       "  .note \"from funcs {who}\"   # trailing\n"
                       ".end\n");
    ::setenv("PROMPT_FUNCS", shared.c_str(), 1);
    function_table table;
    std::string error;
    check(load_function_files(table, {}, error), "global: loads cleanly");
    check(table.contains("global"), "global: the function is registered");

    auto const called = expand_functions(std::vector<std::string>{"prompt", ".global", "x"}, table, true);
    check(called.error.empty(), "global: call expands");
    check(same(called.storage, {"prompt", ".note", "from funcs x"}), "global: tokenizer comments/quotes apply");

    // The bare first token also names a function.
    auto const bare = expand_functions(std::vector<std::string>{"prompt", "global", "y"}, table, true);
    check(bare.error.empty(), "global: bare head call");
    check(same(bare.storage, {"prompt", ".note", "from funcs y"}), "global: bare head expands");
    ::unsetenv("PROMPT_FUNCS");

    auto const stray = dir / "stray";
    write_file(stray, ".note nope\n");
    ::setenv("PROMPT_FUNCS", stray.c_str(), 1);
    function_table bad;
    check(!load_function_files(bad, {}, error), "global: a non-definition line is an error");
    check(error.contains("only .func blocks"), "global: names the file problem");

    auto const dup = dir / "dup";
    write_file(dup, ".func f .note a .end\n.func f .note b .end\n");
    ::setenv("PROMPT_FUNCS", dup.c_str(), 1);
    function_table twice;
    check(!load_function_files(twice, {}, error), "global: a duplicate inside one file is an error");
    ::unsetenv("PROMPT_FUNCS");
}

void test_collect_inline() {
    // A plain definition yields its name; the signature describes its params.
    static constexpr std::array<std::string_view, 3> plain{".func", "one", ".end"};
    auto const defs = collect_inline_functions(plain);
    check(defs.size() == 1 && defs[0].name == "one", "collect: name");
    check(function_signature(defs[0]) == "function", "collect: a no-arg signature");

    static constexpr std::array<std::string_view, 3> params{".func", "greet(who,ai=grok)", ".end"};
    auto const p = collect_inline_functions(params);
    check(p.size() == 1 && function_signature(p[0]) == "function(who, ai=grok)", "collect: signature with a default");

    // A nested compiled body does not close the definition, and definitions come
    // back in the order they appear.
    static constexpr std::array<std::string_view, 11> nested{
        ".func", "a", ".each", ".note", "x", ".end", ".end", ".func", "b", ".end", ".a",
    };
    auto const n = collect_inline_functions(nested);
    check(n.size() == 2 && n[0].name == "a" && n[1].name == "b", "collect: nesting and order");

    // Malformed definitions are skipped, never fatal.
    static constexpr std::array<std::string_view, 4> malformed{".func", "bad(", ".note", "x"};
    check(collect_inline_functions(malformed).empty(), "collect: a bad signature is skipped");
    static constexpr std::array<std::string_view, 2> unterminated{".func", "half"};
    check(collect_inline_functions(unterminated).empty(), "collect: an unterminated block is skipped");
    static constexpr std::array<std::string_view, 2> none{".note", "hi"};
    check(collect_inline_functions(none).empty(), "collect: no definitions yields nothing");
}

void test_override(std::filesystem::path const& dir) {
    auto const user = dir / "user-funcs";
    auto const project = dir / "project-funcs";
    write_file(user, ".func f .note user .end\n");
    write_file(project, ".func f .note project .end\n");
    auto const list = user.string() + ":" + project.string();
    ::setenv("PROMPT_FUNCS", list.c_str(), 1);
    function_table table;
    std::string error;
    check(load_function_files(table, {}, error), "override: loads both sources");
    auto const r = expand_functions(std::vector<std::string>{"prompt", ".f"}, table, true);
    check(same(r.storage, {"prompt", ".note", "project"}), "override: a later source replaces an earlier one");
    ::unsetenv("PROMPT_FUNCS");
}

} // namespace

int main() {
    auto const dir = scratch_dir();
    test_definition_and_call();
    test_params();
    test_specials();
    test_unknown_placeholder_kept();
    test_argument_stops_at_step();
    test_body_composition();
    test_errors();
    test_passthrough();
    test_collect_inline();
    test_global_files(dir);
    test_override(dir);
    std::filesystem::remove_all(dir);

    if (failures != 0) std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
