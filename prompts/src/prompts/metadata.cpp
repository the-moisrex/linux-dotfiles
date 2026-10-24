#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <array>
#include <string>
#include <string_view>

namespace prompt::prompts {

static void execute_metadata(view const& v, result& r) {
    // metadata.sh calls init_prompt (validating --head, whose value is never
    // used) so --head errors are the only failure mode besides --format's.
    head_lines_or_throw(v);

    // metadata.sh's loop walks the ORIGINAL arguments, ignoring everything
    // that is not --format/-f.
    std::string format = "json";
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        if (v.args[i] == "--format" || v.args[i] == "-f") {
            if (i + 1 >= v.args.size()) {
                throw usage_error{"Missing value for --format\n"};
            }
            format = std::string(v.args[i + 1]);
            ++i;
        }
    }

    // bash's ${format^^} — ASCII upper-casing.
    std::string upper = format;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }

    std::string output;
    output +=
        R"PROMPT(Analyze the input and generate comprehensive metadata representing its core attributes, entities, purpose, and context.
)PROMPT";
    output += std::format("Output the metadata strictly in {} format.\n", upper);
    output += "Ensure the output is well-structured, valid " + format +
              ", and enclosed in a single markdown code block so it can be easily extracted and "
              "parsed.\n";
    output += '\n';

    r.out = std::move(output);
}

static void render_help_metadata(std::ostream& os) {
    os << R"EOF(Usage: prompt metadata [--head N] [--format FORMAT] [FILE...]
       some-command | prompt metadata [--head N] [--format FORMAT] [FILE...]

Analyze the provided inputs and generate structured metadata representing them.

Options:
  --head N         Keep only the first N lines of the embedded context
  --format, -f     Output format for the metadata (e.g., json, xml, yaml, toml, conf). Default: json
)EOF";
}

// The output formats metadata.sh documents in its help text.
static void complete_metadata(completion_request const& req, std::ostream& os) {
    static constexpr std::array<std::string_view, 5> kFormats = {"json", "xml", "yaml", "toml", "conf"};
    if (prompt::completion::write_values_after_flag(os, req, "--format", kFormats)) return;
    prompt::completion::write_values_after_flag(os, req, "-f", kFormats);
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("metadata",
                            "Analyze the provided inputs and generate structured metadata representing "
                            "them.",
                            &execute_metadata, &render_help_metadata, &complete_metadata);

} // namespace prompt::prompts
