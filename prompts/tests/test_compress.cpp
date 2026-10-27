// The compression engine and its units, exercised on plain strings: the
// compressors only ever see a `region`, so no fixture needs a real file. The
// prompt-level CLI (`.compress`, `.compress.list`, `.project`) is covered by the
// ctest smokes in tests/CMakeLists.txt.
#include "../src/compress/compress.hpp"
#include "../src/sdk/signals.hpp"
#include <cstdio>
#include <exception>
#include <format>
#include <string>
#include <string_view>
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

void check_eq(std::string_view got, std::string_view want, char const* what) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s\n  got:  [%.*s]\n  want: [%.*s]\n", what, static_cast<int>(got.size()),
                     got.data(), static_cast<int>(want.size()), want.data());
        ++failures;
    }
}

void check_contains(std::string_view haystack, std::string_view needle, char const* what) {
    if (!haystack.contains(needle)) {
        std::fprintf(stderr, "FAIL: %s (missing [%.*s])\n", what, static_cast<int>(needle.size()), needle.data());
        ++failures;
    }
}

void check_absent(std::string_view haystack, std::string_view needle, char const* what) {
    if (haystack.contains(needle)) {
        std::fprintf(stderr, "FAIL: %s (unexpected [%.*s])\n", what, static_cast<int>(needle.size()), needle.data());
        ++failures;
    }
}

// Empty signals/options, named so `readability-trailing-comma` does not misread
// an inline `{}` argument followed by a comma as a trailing comma.
compress::signals const no_signals{};
compress::options const no_options{};

compress::outcome run(std::string_view name, compress::region const& r, compress::signals const& s = no_signals) {
    compress::compressor const* unit = compress::find_compressor(name);
    check(unit != nullptr, "compressor is registered");
    return unit != nullptr ? unit->compress(r, s) : compress::outcome{};
}

// -- signals ----------------------------------------------------------------

void test_signals() {
    auto const failed = signals::failed_tests("[  FAILED  ] Math.Divides\n[  FAILED  ] Math.Adds\n");
    check(failed.size() == 2, "two failed tests");
    check(failed.size() == 2 && failed[0] == "Math.Divides", "failed tests keep source order");

    auto const focus = signals::extract_focus("a.cpp:12:5: error: boom\nFile \"b.py\", line 7\n");
    check(focus.size() == 2, "two error sites");
    check(focus.size() == 2 && focus[0] == "a.cpp:12", "compiler error site");
    check(focus.size() == 2 && focus[1] == "b.py:7", "python traceback site");

    auto const joined = signals::join_focus(focus);
    check_eq(joined, "a.cpp:12\nb.py:7", "join_focus");
    check(signals::parse_focus(joined) == focus, "parse_focus round-trips join_focus");

    check(signals::looks_like_failure("[  FAILED  ] x"), "looks_like_failure positive");
    check(!signals::looks_like_failure("all good"), "looks_like_failure negative");
    check_eq(signals::to_string(signals::intent_from("fix")), "fix", "intent round-trip");
    check(signals::intent_from("nonsense") == signals::intent::general, "unknown intent is general");
}

// -- cpp.symbols ------------------------------------------------------------

void test_cpp_symbols() {
    compress::region r;
    r.path = "a.cpp";
    r.lang = "cpp";
    r.text = "#include <vector>\n"
             "\n"
             "// Adds two numbers.\n"
             "int add(int a, int b) {\n"
             "    return a + b;\n"
             "}\n"
             "\n"
             "std::vector<int> make(int n) {\n"
             "    std::vector<int> out;\n"
             "    out.push_back(n);\n"
             "    return out;\n"
             "}\n";

    auto const o = run("cpp.symbols", r);
    check(o.changed, "cpp.symbols changed");
    check_contains(o.markdown, "int add(int a, int b)", "cpp.symbols keeps the signature");
    check_absent(o.markdown, "return a + b", "cpp.symbols elides the body");
    check_contains(o.legend, "cpp.symbols", "cpp.symbols legend names the unit");
}

// -- csv.sample -------------------------------------------------------------

void test_csv_sample() {
    std::string csv = "date,symbol,close\n";
    for (int i = 1; i <= 20; ++i) csv += std::format("1405/07/{:02},foolad,{}\n", i, 1000 + i);

    compress::region r;
    r.path = "b.csv";
    r.lang = "csv";
    r.text = csv;

    auto const o = run("csv.sample", r);
    check(o.changed, "csv.sample changed");
    check(o.markdown.starts_with("date,symbol,close\n"), "csv.sample keeps the header first");
    check_contains(o.markdown, "1405/07/01,foolad,1001", "csv.sample keeps the first rows");
    check_absent(o.markdown, "1405/07/20", "csv.sample drops the tail");
    check_contains(o.legend, "csv.sample", "csv.sample legend names the unit");
    check_contains(o.legend, "3 columns", "csv.sample legend counts columns");
    check_contains(o.legend, "20 rows", "csv.sample legend counts rows");
}

void test_csv_small_untouched() {
    compress::region r;
    r.path = "tiny.csv";
    r.lang = "csv";
    r.text = "a,b\n1,2\n3,4\n";
    check(!run("csv.sample", r).changed, "a short csv is left alone");
}

void test_csv_sniffed_from_text() {
    // No path and no language: the content sniff must still claim a table.
    std::string csv;
    for (int i = 0; i < 12; ++i) csv += std::format("{},{}\n", i, i * 2);
    compress::region r;
    r.text = csv;
    check(run("csv.sample", r).changed, "a bare csv block is sniffed");
}

// -- gtest.units ------------------------------------------------------------

void test_gtest_units() {
    std::string body;
    for (int i = 1; i <= 6; ++i) {
        body += std::format("TEST(Math, T{}) {{\n    EXPECT_EQ({} + 0, {});\n}}\n\n", i, i, i);
    }
    compress::region r;
    r.path = "t.cpp";
    r.lang = "cpp";
    r.text = body;

    auto const general = run("gtest.units", r);
    check(general.changed, "gtest.units changed in general mode");
    check_contains(general.legend, "gtest.units", "gtest.units legend names the unit");

    compress::signals fix;
    fix.mode = signals::intent::fix;
    fix.focus = {"Math.T3"};
    auto const fixed = run("gtest.units", r, fix);
    check(fixed.changed, "gtest.units changed in fix mode");
    check_contains(fixed.markdown, "EXPECT_EQ(3 + 0, 3)", "gtest.units keeps the failed body");
    check_absent(fixed.markdown, "EXPECT_EQ(6 + 0, 6)", "gtest.units elides the far passing bodies");
    check_contains(fixed.legend, "Only failed bodies are kept", "fix legend explains the rule");
}

void test_gtest_fix_without_failure_is_untouched() {
    compress::region r;
    r.path = "t.cpp";
    r.lang = "cpp";
    r.text = "TEST(Math, T1) {\n    EXPECT_TRUE(true);\n}\n\nTEST(Math, T2) {\n    EXPECT_TRUE(true);\n}\n";
    compress::signals fix;
    fix.mode = signals::intent::fix;
    check(!run("gtest.units", r, fix).changed, "fix mode without a failure keeps the whole file");
}

// -- the document engine ----------------------------------------------------

std::string fenced_cpp() {
    std::string doc = "before\n\n```cpp\n";
    for (int i = 0; i < 8; ++i) doc += std::format("int f{}() {{\n    return {};\n}}\n\n", i, i);
    doc += "```\n\nafter\n";
    return doc;
}

void test_document() {
    compress::options opt;
    opt.force = true;

    std::string const doc = fenced_cpp();
    auto const rep = compress::compress_document(doc, no_signals, opt);
    check(rep.changed, "document changed");
    check(rep.regions == 1, "one region compressed");
    check_contains(rep.text, "[compressed]", "the summary line is prepended");
    check_contains(rep.text, "before", "literal text before the fence is kept");
    check_contains(rep.text, "after", "literal text after the fence is kept");
    check_contains(rep.text, "int f0()", "signature kept");
    check_absent(rep.text, "return 0;", "body elided");
}

void test_document_unchanged_round_trips() {
    auto const rep = compress::compress_document("just text\n", no_signals, no_options);
    check(!rep.changed, "plain text is unchanged");
    check_eq(rep.text, "just text\n", "plain text round-trips byte-for-byte");
}

void test_focal_region_is_untouched() {
    compress::options opt;
    opt.force = true;
    compress::signals focused;
    focused.focus = {"a.cpp"};

    std::string const doc = "### a.cpp\n\n```cpp\nint f() {\n    return 1;\n}\n```\n";
    auto const rep = compress::compress_document(doc, focused, opt);
    check(!rep.changed, "a focal region is never compressed");

    auto const lines = compress::explain(doc, focused, opt);
    check(lines.size() == 1 && lines[0].contains("keep (focal)"), "explain reports the focal region");
}

void test_budget_raises_the_level() {
    std::string const doc = fenced_cpp();
    compress::options loose;
    loose.force = true;
    compress::options tight = loose;
    tight.budget_lines = 1;
    auto const a = compress::compress_document(doc, no_signals, loose);
    auto const b = compress::compress_document(doc, no_signals, tight);
    check(a.changed && b.changed, "both budgeted renders changed");
    check(b.after <= a.after, "a tight budget never grows the output");
}

void test_only_and_skip() {
    compress::options skip;
    skip.force = true;
    skip.skip = {"cpp.symbols"};
    std::string const doc = fenced_cpp();
    check(!compress::compress_document(doc, no_signals, skip).changed, "--skip disables the unit");

    compress::options only;
    only.force = true;
    only.only = {"csv.sample"};
    check(!compress::compress_document(doc, no_signals, only).changed, "--only restricts to the unit");
}

void test_catalog() {
    check(compress::find_compressor("cpp.symbols") != nullptr, "cpp.symbols registered");
    check(compress::find_compressor("csv.sample") != nullptr, "csv.sample registered");
    check(compress::find_compressor("gtest.units") != nullptr, "gtest.units registered");
    check(compress::find_compressor("include.map") != nullptr, "include.map registered");
    check(compress::find_compressor("no.such.unit") == nullptr, "unknown unit is not found");
    check(compress::get_all_compressors().size() >= 4, "the catalog lists the units");
}

} // namespace

int main() {
    try {
        test_signals();
        test_cpp_symbols();
        test_csv_sample();
        test_csv_small_untouched();
        test_csv_sniffed_from_text();
        test_gtest_units();
        test_gtest_fix_without_failure_is_untouched();
        test_document();
        test_document_unchanged_round_trips();
        test_focal_region_is_untouched();
        test_budget_raises_the_level();
        test_only_and_skip();
        test_catalog();
    } catch (std::exception const& error) {
        std::fprintf(stderr, "FAIL: unexpected exception: %s\n", error.what());
        return 1;
    }

    if (failures == 0) {
        std::printf("all compress tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d compress test(s) failed\n", failures);
    return 1;
}
