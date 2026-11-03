// The tool-schema layer: the factories, the help-derived fallback and the
// renderers/completion a prompt builds from a schema. The prompt-level users
// (`.glob`, `.grep`, `.tools`) are covered by the ctest smokes in
// tests/CMakeLists.txt.
#include "../src/sdk/schema.hpp"
#include <cstdint>
#include <cstdio>
#include <ostream>
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

void check_contains(std::string_view haystack, std::string_view needle, char const* what) {
    if (!haystack.contains(needle)) {
        std::fprintf(stderr, "FAIL: %s (missing [%.*s])\n", what, static_cast<int>(needle.size()), needle.data());
        ++failures;
    }
}

// -- factories and rendering ------------------------------------------------

void test_factories() {
    schema::tool const tool = schema::make_tool("demo", "A demo tool.",
                                                {
                                                    schema::bool_flag("verbose", "Be loud", "v"),
                                                    schema::int_flag("head", "Keep N lines"),
                                                    schema::str_flag("format", "Output format", "FMT"),
                                                    schema::enum_flag("mode", "Pick a mode", {"fast", "slow"}, "m"),
                                                },
                                                {
                                                    schema::argument("FILE", "The input", true),
                                                    schema::argument("EXTRA", "More", false, true),
                                                });

    check(tool.name == "demo", "make_tool keeps the name");
    check(tool.parameters.size() == 4, "make_tool keeps every parameter");
    check(tool.parameters[0].kind == schema::type::boolean, "bool_flag is boolean");
    check(tool.parameters[0].alias == "v", "bool_flag keeps the alias");
    check(tool.parameters[1].kind == schema::type::integer, "int_flag is integer");
    check(tool.parameters[2].takes_value, "str_flag takes a value");
    check(tool.parameters[2].value_placeholder == "FMT", "str_flag keeps the placeholder");
    check(tool.parameters[3].enum_values.size() == 2, "enum_flag keeps its values");
    check(tool.operands.size() == 2 && tool.operands[1].variadic, "argument keeps variadic");

    std::string const markdown = schema::to_markdown(tool);
    check_contains(markdown, "### demo", "to_markdown has a heading");
    check_contains(markdown, "A demo tool.", "to_markdown has the summary");
    check_contains(markdown, "-v, --verbose", "to_markdown shows an alias");
    check_contains(markdown, "--head N", "to_markdown shows a value placeholder");
    check_contains(markdown, "FILE (required)", "to_markdown marks a required operand");

    std::string const json = schema::to_json(tool);
    check_contains(json, R"("name": "demo")", "to_json has the name");
    check_contains(json, R"("type": "boolean")", "to_json maps the boolean type");
    check_contains(json, R"("type": "integer")", "to_json maps the integer type");
    check_contains(json, R"("enum")", "to_json carries the enum values");
    check_contains(json, R"("x-flag": "--head")", "to_json carries the flag spelling");

    std::string const options = schema::render_options(tool);
    check_contains(options, "Options:", "render_options has the header");
    check_contains(options, "--mode MODE", "render_options shows the enum placeholder");
}

// -- from_help: flags derived from a prompt's own Options block --------------

void test_from_help() {
    std::string_view const help = R"(Usage: prompt demo [--head N] [-i] [--format FMT] FILE...

A demo.

Options:
  --head N            Keep only the first N lines
  -i, --ignore-case   Case-insensitive
  --format, -f FMT    Output format
  --num, --numbered   Number the lines
  -h, --help          Show this help message

)";
    schema::tool const tool = schema::from_help("demo", "A demo.", help);

    check(schema::find_tool("demo") == nullptr, "from_help does not register");
    check(tool.parameters.size() >= 5, "from_help reads every flag");

    bool head = false;
    bool ignore = false;
    bool fmt = false;
    bool numbered = false;
    for (auto const& par : tool.parameters) {
        if (par.name == "head") {
            head = par.kind == schema::type::integer && par.takes_value;
        } else if (par.name == "ignore-case") {
            ignore = !par.takes_value && par.alias == "i";
        } else if (par.name == "format") {
            fmt = par.takes_value && par.alias == "f" && par.value_placeholder == "FMT";
        } else if (par.name == "numbered") {
            numbered = !par.takes_value;
        }
    }
    check(head, "from_help reads a value flag with an integer placeholder");
    check(ignore, "from_help reads a short alias with no value");
    check(fmt, "from_help reads a comma-separated long/short flag with a placeholder");
    check(numbered, "from_help reads a second value-less spelling");
}

// -- registration and completion --------------------------------------------

void test_registry_and_completion() {
    bool const ok = schema::register_tool(schema::make_tool("test.schema", "A registered test tool.",
                                                            {
                                                                schema::int_flag("head", "Keep N lines"),
                                                                schema::enum_flag("mode", "M", {"fast", "slow"}),
                                                            }));
    check(ok, "register_tool succeeds");
    check(schema::find_tool("test.schema") != nullptr, "find_tool returns the registered tool");
    check(schema::find_tool("no.such.tool") == nullptr, "find_tool misses an unknown name");

    bool present = false;
    for (auto const& tool : schema::all_tools()) present = present || tool.name == "test.schema";
    check(present, "all_tools includes the registered tool");

    schema::tool const* tool = schema::find_tool("test.schema");
    auto complete = [&](std::string_view prefix, std::vector<std::string_view> const& prior) {
        std::ostringstream out;
        completion_request const request{.prefix = prefix, .prior_args = prior};
        schema::complete(*tool, request, out);
        return out.str();
    };
    check_contains(complete("--", {}), "--head", "completion offers a flag");
    check_contains(complete("--mode", {}), "--mode", "completion offers the enum flag itself");
    check_contains(complete("f", {"--mode"}), "fast", "completion offers an enum value after its flag");
    check(complete("f", {"--head"}).empty(), "no enum completion after a non-enum flag");
}

} // namespace

int main() {
    test_factories();
    test_from_help();
    test_registry_and_completion();
    if (failures != 0) {
        std::fprintf(stderr, "%d schema check(s) failed\n", failures);
        return 1;
    }
    std::printf("all schema tests passed\n");
    return 0;
}
