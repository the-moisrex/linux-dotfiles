#include "../src/core/strings.hpp"
#include <cstdio>
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

void test_strip_newlines() {
    check_eq(text::strip_newlines("\n\nhi\n\n"), "hi", "strip_newlines drops both edges");
    check_eq(text::strip_newlines("  hi  \n"), "  hi  ", "strip_newlines keeps spaces and tabs");
    check_eq(text::strip_newlines("\n\n"), "", "strip_newlines of all newlines is empty");
    check_eq(text::strip_newlines("a\n\nb"), "a\n\nb", "strip_newlines keeps an interior blank line");
    check_eq(text::strip_newlines(""), "", "strip_newlines of empty is empty");
}

void test_append_block() {
    std::string out;
    check(!text::append_block(out, "\n\n"), "append_block skips an empty block");
    check(out.empty(), "append_block left the buffer empty");

    check(text::append_block(out, "\n\nalpha\n\n"), "append_block adds a block");
    check_eq(out, "alpha\n", "append_block trims and terminates");

    check(text::append_block(out, "beta"), "append_block adds a second block");
    check_eq(out, "alpha\n\nbeta\n", "append_block separates with exactly one blank line");

    // An empty block never opens a separator, so a later block stays adjacent.
    check(!text::append_block(out, "\n\n"), "append_block skips another empty block");
    check_eq(out, "alpha\n\nbeta\n", "append_block kept the buffer unchanged");
}

} // namespace

int main() {
    test_strip_newlines();
    test_append_block();
    if (failures == 0) {
        std::printf("all string tests passed\n");
        return 0;
    }
    return 1;
}
