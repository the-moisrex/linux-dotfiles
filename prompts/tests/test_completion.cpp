// Shell completion (`prompt <name> --complete ...`): the dispatcher derives
// every prompt's flags from its own help text, and a prompt's optional
// complete_fn adds positional/value candidates. These tests exercise the
// parsing helpers directly and check the end-to-end wiring through the
// registry (which prompts registered a callback, and what their help yields).
#include "../src/sdk/completion.hpp"
#include "../src/sdk/prompt_registry.hpp"
#include <array>
#include <cstdio>
#include <sstream>
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

// True when `line` appears as a whole line (`value` or `value<TAB>description`).
bool has_line(std::string_view text, std::string_view line) {
    std::string const needle = "\n" + std::string(line) + "\n";
    std::string const prefix = std::string(line) + "\n";
    std::string const body(text);
    return body.contains(needle) || text.starts_with(prefix);
}

template <typename Fn>
std::string render(Fn&& fn) {
    std::ostringstream os;
    fn(os);
    return os.str();
}

completion_request request(std::string_view prefix, std::vector<std::string_view> const& prior) {
    return completion_request{.prefix = prefix, .prior_args = std::span<std::string_view const>{prior}};
}

// `matches` is a plain prefix test, with the empty prefix matching everything.
void test_matches() {
    check(completion::matches("", "--head"), "empty prefix matches everything");
    check(completion::matches("--", "--head"), "flag prefix matches a flag");
    check(!completion::matches("--x", "--head"), "unrelated prefix does not match");
}

// Flag completion is derived from the prompt's own `Options:` block: both
// spellings of a comma-separated option, at most the matching ones.
void test_help_flags() {
    std::string_view help = "Usage: prompt demo [--head N]\n"
                            "\n"
                            "Options:\n"
                            "  --head N     Keep only the first N lines\n"
                            "  --type, -t   Diagram type\n"
                            "  -h, --help   Show this help\n"
                            "\n"
                            "Examples:\n"
                            "  prompt demo --head 5\n";
    std::string all = render([&](std::ostream& os) { completion::write_help_flags(os, "--", help); });
    check(has_line(all, "--head"), "help flags: --head");
    check(has_line(all, "--type"), "help flags: --type");
    check(has_line(all, "--help"), "help flags: --help");

    std::string short_flags = render([&](std::ostream& os) { completion::write_help_flags(os, "-t", help); });
    check(has_line(short_flags, "-t"), "help flags: -t");
    check(!has_line(short_flags, "--type"), "help flags: -t does not match --type");

    // A non-dash prefix never returns flags, and a help without Options yields
    // nothing.
    check(render([&](std::ostream& os) { completion::write_help_flags(os, "f", help); }).empty(),
          "help flags need a dash prefix");
    check(render([&](std::ostream& os) { completion::write_help_flags(os, "--", "Usage: prompt x\n"); }).empty(),
          "help flags need an Options block");
}

// `write_values_after_flag` completes a flag's value only right after the flag.
void test_value_after_flag() {
    static constexpr std::array<std::string_view, 3> kValues = {"json", "xml", "yaml"};
    std::vector<std::string_view> after{"--format"};
    std::string out = render(
        [&](std::ostream& os) { completion::write_values_after_flag(os, request("", after), "--format", kValues); });
    check(has_line(out, "json") && has_line(out, "xml") && has_line(out, "yaml"), "value completion lists every value");

    std::string filtered = render(
        [&](std::ostream& os) { completion::write_values_after_flag(os, request("y", after), "--format", kValues); });
    check(has_line(filtered, "yaml") && !has_line(filtered, "json"), "value completion filters by prefix");

    std::vector<std::string_view> elsewhere{"--head"};
    check(render([&](std::ostream& os) {
              completion::write_values_after_flag(os, request("", elsewhere), "--format", kValues);
          }).empty(),
          "value completion requires the flag immediately before");
}

// Values carry an optional description after a tab, exactly how fish reads them.
void test_items_with_descriptions() {
    static constexpr std::array<completion::item, 2> kItems = {
        {
            {.value = "list", .description = "List cached skills"},
            {.value = "install", .description = "Download a skill"},
        },
    };
    std::string out = render([&](std::ostream& os) { completion::write_items(os, "i", kItems); });
    check(out == "install\tDownload a skill\n", "items keep their tab-separated description");
}

// End-to-end wiring: the prompts that know their own operands registered a
// complete_fn, and their help text feeds the flag parser. `tse`/`tse.intraday`/
// `tse.codal` are only checked for registration — invoking them would shell out
// to `bin/tse.resolve`. `tse.analyze`/`tse.intraday.analysis` take no arguments,
// so they register no complete_fn.
void test_registry_wiring() {
    for (char const* name : {
             "tse",
             "tse.intraday",
             "tse.codal",
             "run",
             "skill",
             "diagram",
             "metadata",
             "opportunities",
             "overengineering",
         }) {
        auto const* desc = find_prompt(name);
        check(desc != nullptr, "prompt is registered");
        if (desc != nullptr) {
            check(desc->complete_fn != nullptr, "prompt registered a complete_fn");
        }
    }

    check(find_prompt("tse.analyze") != nullptr, "tse.analyze is registered");
    check(find_prompt("tse.analyze") != nullptr && find_prompt("tse.analyze")->complete_fn == nullptr,
          "tse.analyze takes no arguments, so it registers no complete_fn");

    check(find_prompt("tse.analyze.history") != nullptr, "tse.analyze.history is registered");
    check(find_prompt("tse.analyze.history") != nullptr && find_prompt("tse.analyze.history")->complete_fn == nullptr,
          "tse.analyze.history takes no arguments, so it registers no complete_fn");

    check(find_prompt("tse.intraday.analysis") != nullptr, "tse.intraday.analysis is registered");
    check(find_prompt("tse.intraday.analysis") != nullptr &&
              find_prompt("tse.intraday.analysis")->complete_fn == nullptr,
          "tse.intraday.analysis takes no arguments, so it registers no complete_fn");

    auto const* metadata = find_prompt("metadata");
    if (metadata != nullptr && metadata->render_help_fn != nullptr) {
        std::string help = render([&](std::ostream& os) { metadata->render_help_fn(os); });
        std::string flags = render([&](std::ostream& os) { completion::write_help_flags(os, "--", help); });
        check(has_line(flags, "--format"), "metadata help advertises --format");
        check(has_line(flags, "--head"), "metadata help advertises --head");
    }

    auto const* diagram = find_prompt("diagram");
    if (diagram != nullptr && diagram->complete_fn != nullptr) {
        std::vector<std::string_view> after{"--type"};
        std::string out = render([&](std::ostream& os) { diagram->complete_fn(request("mer", after), os); });
        check(has_line(out, "mermaid"), "diagram completes mermaid after --type");
    }
}

} // namespace

int main() {
    test_matches();
    test_help_flags();
    test_value_after_flag();
    test_items_with_descriptions();
    test_registry_wiring();
    if (failures == 0) {
        std::printf("all completion tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d completion checks failed\n", failures);
    return 1;
}
