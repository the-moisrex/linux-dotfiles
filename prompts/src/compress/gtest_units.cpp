#include "../sdk/project.hpp"
#include "compress.hpp"
#include "cpp_scan.hpp"
#include "cpp_symbols.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::compress {

namespace {

bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

std::string_view last_identifier(std::string_view s) {
    std::size_t i = s.size();
    while (i > 0) {
        while (i > 0 && !word_char(s[i - 1])) --i;
        std::size_t const end = i;
        while (i > 0 && word_char(s[i - 1])) --i;
        if (end > i) return s.substr(i, end - i);
    }
    return {};
}

// Index just past the brace matching `code[open]` (`o`/`c` are the same pair
// for `{`/`}` or `(`/`)`). The structural view has comments and strings blanked,
// so a plain depth count is enough.
std::size_t matching(std::string_view code, std::size_t open, char o, char c) {
    int depth = 0;
    for (std::size_t i = open; i < code.size(); ++i) {
        if (code[i] == o) {
            ++depth;
        } else if (code[i] == c && --depth == 0) {
            return i + 1;
        }
    }
    return std::string_view::npos;
}

struct test_case {
    std::string suite;
    std::string name;
    std::string display;
    std::size_t body_open = 0;  // index of the body's `{`
    std::size_t body_close = 0; // index just past the body's `}`
    bool failed = false;
};

// Parse one `TEST*` macro starting at `i` (which must be the macro's first
// character), or nullopt when this is not a test definition with a body.
std::optional<test_case> parse_test_at(std::string_view code, std::size_t i) {
    static constexpr std::array<std::string_view, 5> macros = {
        "TYPED_TEST_P", "TYPED_TEST", "TEST_P", "TEST_F", "TEST",
    };
    for (auto const macro : macros) {
        if (i + macro.size() > code.size() || code.substr(i, macro.size()) != macro) continue;
        if (i > 0 && word_char(code[i - 1])) continue;
        std::size_t j = i + macro.size();
        while (j < code.size() && (code[j] == ' ' || code[j] == '\t')) ++j;
        if (j >= code.size() || code[j] != '(') continue;

        std::size_t const close_paren = matching(code, j, '(', ')');
        if (close_paren == std::string_view::npos) return std::nullopt;
        std::string_view const args = code.substr(j + 1, close_paren - j - 2);
        std::size_t const comma = args.find(',');
        if (comma == std::string_view::npos) return std::nullopt;
        std::string_view const suite = last_identifier(args.substr(0, comma));
        std::string_view const name = last_identifier(args.substr(comma + 1));
        if (suite.empty() || name.empty()) return std::nullopt;

        std::size_t k = close_paren;
        while (k < code.size() && std::isspace(static_cast<unsigned char>(code[k])) != 0) ++k;
        if (k >= code.size() || code[k] != '{') return std::nullopt;
        std::size_t const close_body = matching(code, k, '{', '}');
        if (close_body == std::string_view::npos) return std::nullopt;

        test_case tc;
        tc.suite = std::string(suite);
        tc.name = std::string(name);
        tc.display = tc.suite + "." + tc.name;
        tc.body_open = k;
        tc.body_close = close_body;
        return tc;
    }
    return std::nullopt;
}

std::vector<test_case> find_tests(std::string_view code) {
    std::vector<test_case> cases;
    std::size_t i = 0;
    while (i < code.size()) {
        if (!word_char(code[i])) {
            ++i;
            continue;
        }
        if (auto tc = parse_test_at(code, i)) {
            i = tc->body_close;
            cases.push_back(std::move(*tc));
            continue;
        }
        ++i;
    }
    return cases;
}

// GTest names a parameterized failure `Inst/Suite.Test/0`; the display name is
// always a substring, so a containment test matches both shapes.
bool held(test_case const& tc, std::vector<std::string> const& focus) {
    return std::ranges::any_of(focus, [&](std::string const& entry) { return entry.contains(tc.display); });
}

std::size_t levenshtein(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1);
    std::vector<std::size_t> curr(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        curr[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t const cost = a[i - 1] == b[j - 1] ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    return prev[b.size()];
}

bool applies_gtest(region const& r, [[maybe_unused]] signals const& s) {
    bool const cpp =
        is_cpp_lang(r.lang) || (r.lang.empty() && (project::language_for_path(r.path) == project::language::cpp ||
                                                   project::language_for_path(r.path) == project::language::c));
    if (!cpp) return false;
    static constexpr std::array<std::string_view, 4> needles = {"TEST(", "TEST_F(", "TEST_P(", "TYPED_TEST"};
    return std::ranges::any_of(needles, [&](std::string_view needle) { return r.text.contains(needle); });
}

outcome run_gtest(region const& r, signals const& s) {
    std::string const code = cpp::structural_view(r.text);
    std::vector<test_case> cases = find_tests(code);

    // A `TEST(` in a comment or a helper name: this is not a test file, so let
    // the C++ symbol compressor have it.
    if (cases.empty()) return cpp_symbols::compress(r, s);

    for (auto& tc : cases) tc.failed = held(tc, s.focus);
    auto const failed_count = static_cast<std::size_t>(
        std::count_if(cases.begin(), cases.end(), [](test_case const& tc) { return tc.failed; }));

    bool const general = s.mode == prompt::signals::intent::general || s.mode == prompt::signals::intent::review;
    if (!general && failed_count == 0) {
        // Nothing identifies a failure: keep the whole file rather than feed the
        // model a compressed view of a suite that might be hiding the one test
        // that matters.
        return {};
    }

    // Keep failed bodies always. In general mode, also sample a couple of
    // successful tests nearest the failures (by name), or the first few when no
    // failure is known.
    std::size_t sample_limit = 0;
    if (s.level <= 1) sample_limit = failed_count == 0 ? 3 : 2;
    std::vector<bool> keep(cases.size(), false);
    for (std::size_t i = 0; i < cases.size(); ++i) keep[i] = cases[i].failed;
    if (sample_limit > 0) {
        std::vector<std::pair<std::size_t, std::size_t>> ranked; // distance, index
        for (std::size_t i = 0; i < cases.size(); ++i) {
            if (cases[i].failed) continue;
            std::size_t best = std::string_view::npos;
            if (failed_count == 0) {
                best = i; // stable source order
            }
            for (auto const& other : cases) {
                if (!other.failed) continue;
                best = std::min(best, levenshtein(cases[i].display, other.display));
            }
            ranked.emplace_back(best, i);
        }
        std::stable_sort(ranked.begin(), ranked.end());
        for (std::size_t k = 0; k < ranked.size() && k < sample_limit; ++k) keep[ranked[k].second] = true;
    }

    std::string out;
    std::size_t cursor = 0;
    std::size_t kept_bodies = 0;
    std::size_t elided = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        auto const& tc = cases[i];
        if (tc.body_close <= cursor) continue;
        if (keep[i]) {
            out.append(r.text, cursor, tc.body_close - cursor);
            ++kept_bodies;
        } else {
            out.append(r.text, cursor, tc.body_open + 1 - cursor);
            out += " … }";
            ++elided;
        }
        cursor = tc.body_close;
    }
    out.append(r.text.data() + cursor, r.text.size() - cursor);

    outcome o;
    o.total = count_lines(r.text);
    o.markdown = std::move(out);
    o.kept = count_lines(o.markdown);
    if (elided == 0 || o.kept >= o.total) return {};

    std::string first_failed;
    for (auto const& tc : cases) {
        if (tc.failed) {
            first_failed = tc.display;
            break;
        }
    }

    o.legend = "gtest.units: " + plural(cases.size(), "test", "tests") + ", " +
               plural(failed_count, "failed", "failed") + "; " + plural(kept_bodies, "body kept", "bodies kept") +
               ", " + plural(elided, "body elided", "bodies elided") + ".";
    if (!general) {
        o.legend += " Only failed bodies are kept.";
    } else if (sample_limit > 0 && failed_count > 0) {
        o.legend += " Sampled the successful tests nearest the failures.";
    } else if (sample_limit > 0) {
        o.legend += " Sampled the first few tests.";
    }
    std::string extra;
    if (!first_failed.empty()) extra = "`prompt gtest-case --exact " + first_failed + "`";
    o.legend += expand_hint(r.path, extra);
    o.changed = true;
    return o;
}

} // namespace

bool const registered_gtest = register_compressor(compressor{
    .name = "gtest.units",
    .summary = "Replace a Google Test file with its failed test bodies and a sample of the rest.",
    .applies = &applies_gtest,
    .compress = &run_gtest,
    .priority = 20,
});

} // namespace prompt::compress
