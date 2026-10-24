#include "../src/core/clipboard.hpp"
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check_id(std::optional<std::string> const& got, std::string_view want, char const* what) {
    if (!got) {
        std::fprintf(stderr, "FAIL: %s (no identifier detected)\n", what);
        ++failures;
        return;
    }
    if (*got != want) {
        std::fprintf(stderr, "FAIL: %s\n  got:  [%s]\n  want: [%.*s]\n", what, got->c_str(),
                     static_cast<int>(want.size()), want.data());
        ++failures;
    }
}

void check_none(std::optional<std::string> const& got, char const* what) {
    if (got) {
        std::fprintf(stderr, "FAIL: %s (unexpected identifier [%s])\n", what, got->c_str());
        ++failures;
    }
}

// The URL forms are matched whole, in preference to the bare ISIN/insCode
// tokens they contain.
void test_urls() {
    using prompt::clipboard::detect_identifier;

    check_id(detect_identifier("https://d.easytrader.ir/easy-chart/IRO1FOLD0001"),
             "https://d.easytrader.ir/easy-chart/IRO1FOLD0001", "easytrader chart URL");
    check_id(detect_identifier("see https://d.easytrader.ir/stock-details/IRO1FOLD0001 for details"),
             "https://d.easytrader.ir/stock-details/IRO1FOLD0001", "easytrader URL inside a sentence");
    check_id(detect_identifier("https://www.tsetmc.com/instInfo/123456789012345"),
             "https://www.tsetmc.com/instInfo/123456789012345", "tsetmc URL");
    check_id(detect_identifier("https://tsetmc.com/instInfo/123456789012345"),
             "https://tsetmc.com/instInfo/123456789012345", "tsetmc URL without www");
    check_id(detect_identifier("https://www.codal.ir/ReportList.aspx?Symbol=فولاد&Type=1"),
             "https://www.codal.ir/ReportList.aspx?Symbol=فولاد", "codal report URL stops at &");
    check_id(detect_identifier("https://codal.ir/ReportList.aspx?A=1&Symbol=خودرو"),
             "https://codal.ir/ReportList.aspx?A=1&Symbol=خودرو", "codal URL with a leading parameter");
}

// A bare ISIN or insCode, and a copied Persian symbol, are identifiers too.
void test_tokens_and_symbols() {
    using prompt::clipboard::detect_identifier;

    check_id(detect_identifier("IRO1FOLD0001"), "IRO1FOLD0001", "bare ISIN");
    check_id(detect_identifier("123456789012345"), "123456789012345", "bare insCode");
    check_id(detect_identifier("فولاد"), "فولاد", "Persian symbol");
    check_id(detect_identifier("فولاد "), "فولاد ", "trailing space is part of the copied text");
}

// CR/LF from a terminal copy are stripped before matching.
void test_line_endings() {
    using prompt::clipboard::detect_identifier;

    check_id(detect_identifier("فولاد\n"), "فولاد", "trailing newline");
    check_id(detect_identifier("فولاد\r\n"), "فولاد", "CRLF");
    check_id(detect_identifier("\n\n  IRO1FOLD0001 \n"), "IRO1FOLD0001", "a token inside surrounding whitespace");
    check_none(detect_identifier("\r\n"), "line endings only");
    check_none(detect_identifier(""), "empty text");
}

// A sentence is not an identifier: Latin letters, digits or excessive length.
void test_rejections() {
    using prompt::clipboard::detect_identifier;

    check_none(detect_identifier("hello"), "a Latin word");
    check_none(detect_identifier("فولاد 123"), "a symbol plus digits");
    check_none(detect_identifier("این یک جملهٔ کامل فارسی است که هیچ شناسهای در آن نیست"), "a Persian sentence");

    std::string const max_symbol = [] {
        std::string s;
        while (s.size() < 64) s += "ف"; // 2 bytes each: exactly 64 bytes
        return s;
    }();
    check_id(detect_identifier(max_symbol), max_symbol, "a 64-byte symbol is still accepted");

    std::string const too_long = max_symbol + "ف"; // 66 bytes
    check_none(detect_identifier(too_long), "a symbol over 64 bytes is rejected");
}

} // namespace

int main() {
    test_urls();
    test_tokens_and_symbols();
    test_line_endings();
    test_rejections();

    if (failures == 0) {
        std::printf("all clipboard identifier tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d clipboard identifier check(s) failed\n", failures);
    return 1;
}
