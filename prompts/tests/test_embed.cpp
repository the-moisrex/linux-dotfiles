#include "../src/sdk/args.hpp"
#include "../src/sdk/errors.hpp"
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

using namespace prompt;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void check_eq(std::string_view got, std::string_view want, char const* what) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s\n  got:  [%.*s]\n  want: [%.*s]\n", what, static_cast<int>(got.size()),
                     got.data(), static_cast<int>(want.size()), want.data());
        ++failures;
    }
}

// `view` holds references, so the fixture owns the storage the view points at.
struct fixture {
    std::vector<std::string_view> args;
    facts prior;
    environment env;
    stdin_source in;

    fixture(std::initializer_list<std::string_view> arguments) : args(arguments) {}

    [[nodiscard]] view make() {
        return view{.name = "test", .args = args, .text = {}, .prior = prior, .env = env, .in = in};
    }
};

fixture make_ctx(std::initializer_list<std::string_view> args) { return fixture{args}; }

// --num numbers the kept lines relative to the (possibly --head-trimmed)
// content, right-aligns to the width of the last number, and keeps the input's
// trailing-newline-ness so the surrounding fence stays byte-stable.
void test_number_lines() {
    check_eq(number_lines("a\nb\n", true), "1 | a\n2 | b\n", "numbers each line");
    check_eq(number_lines("a\nb\n", false), "a\nb\n", "leaves the text alone when off");
    check_eq(number_lines("", true), "", "empty text stays empty");

    // Two-digit content right-aligns the single-digit numbers too.
    std::string twelve;
    for (int i = 1; i <= 12; ++i) {
        twelve += "line ";
        twelve += std::to_string(i);
        twelve += '\n';
    }
    check(number_lines(twelve, true).starts_with(" 1 | line 1\n"), "single digits are padded to the width of the last");
    check(number_lines(twelve, true).contains("12 | line 12\n"), "the last line keeps its number");

    // No final newline in, no final newline out.
    check_eq(number_lines("x", true), "1 | x", "a missing trailing newline is preserved");
    check_eq(number_lines("\n", true), "1 | \n", "blank input is one numbered line");
}

void test_file_options_or_throw() {
    {
        auto ctx = make_ctx({"--num"});
        auto opts = file_options_or_throw(ctx.make());
        check(opts.numbered, "--num sets numbered");
        check(!opts.head_lines.has_value(), "--num alone leaves --head unset");
    }
    {
        auto ctx = make_ctx({"--numbered", "--head", "5"});
        auto opts = file_options_or_throw(ctx.make());
        check(opts.numbered, "--numbered sets numbered");
        check(opts.head_lines == 5, "--head keeps its value");
    }
    {
        auto ctx = make_ctx({"file.cpp", "--num"});
        auto opts = file_options_or_throw(ctx.make());
        check(opts.numbered, "the flag is found among the FILE arguments");
    }

    auto expect_message = [](std::initializer_list<std::string_view> args, std::string_view want, char const* what) {
        try {
            auto fixture = make_ctx(args);
            file_options_or_throw(fixture.make());
            std::fprintf(stderr, "FAIL: %s (no exception)\n", what);
            ++failures;
        } catch (prompt_error const& e) {
            check(e.exit_code() == 2, what);
            check_eq(e.what(), want, what);
        }
    };
    expect_message({"--head", "abc"}, "--head requires a non-negative integer\n", "a bad --head is a usage error");
    expect_message({"--head"}, "Missing value for --head\n", "a dangling --head is a usage error");
}

void test_is_number_flag() {
    check(is_number_flag("--num"), "--num is a number flag");
    check(is_number_flag("--numbered"), "--numbered is a number flag");
    check(!is_number_flag("--head"), "--head is not a number flag");
    check(!is_number_flag("--num=1"), "only the exact spellings count");
}

// The FILE-argument helpers drop --head's value and the number flags, so a
// prompt never treats them as paths.
void test_argument_filtering() {
    auto ctx = make_ctx({"--num", "a.cpp", "--head", "3", "--numbered", "b.cpp"});
    auto args = positional_args(ctx.make());
    check(args.size() == 2 && args[0] == "a.cpp" && args[1] == "b.cpp", "positional_args drops the flags");

    auto files = collect_file_args(ctx.make());
    check(files.files.empty(), "non-existent paths are not files");
    check(files.missing.size() == 2 && files.missing[0] == "a.cpp" && files.missing[1] == "b.cpp",
          "collect_file_args reports the missing ones without the flags");
}

// The shared help helpers keep every file prompt's usage and Options block
// identical.
void test_help_helpers() {
    check_eq(file_usage_help("fix"),
             "Usage: prompt fix [--head N] [--num] [FILE...]\n"
             "       some-command | prompt fix [--head N] [--num] [FILE...]\n",
             "default usage lines");
    check_eq(file_usage_help("ci", "", "echo \"task description\""),
             "Usage: prompt ci [--head N] [--num]\n"
             "       echo \"task description\" | prompt ci [--head N] [--num]\n",
             "custom pipe and empty tail");
    check_eq(file_usage_help("skill", "[FILE...]", "some-command", "<name>"),
             "Usage: prompt skill <name> [--head N] [--num] [FILE...]\n"
             "       some-command | prompt skill <name> [--head N] [--num] [FILE...]\n",
             "prefix goes before the flags");
    check_eq(file_usage_help("cmake", "[--exclude PATTERN]", ""),
             "Usage: prompt cmake [--head N] [--num] [--exclude PATTERN]\n", "an empty pipe drops the second line");

    check_eq(file_options_help("the embedded context"),
             "  --head N   Keep only the first N lines of the embedded context\n"
             "  --num      Prefix each embedded line with its line number (alias: --numbered)\n",
             "the shared Options block");
    check(file_options_help("x", true).contains("  -h, --help Show this help message\n"),
          "show_help appends the -h, --help line");
}

} // namespace

int main() {
    test_number_lines();
    test_file_options_or_throw();
    test_is_number_flag();
    test_argument_filtering();
    test_help_helpers();

    if (failures != 0) {
        std::fprintf(stderr, "%d embed/args check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
