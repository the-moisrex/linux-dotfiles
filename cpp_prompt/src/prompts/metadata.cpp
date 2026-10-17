#include "../sdk/args.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_metadata(prompt_context& ctx) {
    // metadata.sh calls init_prompt (validating --head) but never uses ARGS,
    // so --head errors are the only failure mode besides --format's.
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    // metadata.sh's loop walks the ORIGINAL arguments, ignoring everything
    // that is not --format/-f.
    std::string format = "json";
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--format" || ctx.args[i] == "-f") {
            if (i + 1 >= ctx.args_count) {
                throw usage_error{"Missing value for --format\n"};
            }
            format = std::string(ctx.args[i + 1]);
            ++i;
        }
    }

    // bash's ${format^^} — ASCII upper-casing.
    std::string upper = format;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }

    std::string output;
    output += "Analyze the input and generate comprehensive metadata representing its core "
              "attributes, entities, purpose, and context.\n";
    output += std::format("Output the metadata strictly in {} format.\n", upper);
    output += "Ensure the output is well-structured, valid " + format +
              ", and enclosed in a single markdown code block so it can be easily extracted and "
              "parsed.\n";
    output += '\n';

    ctx.out = std::move(output);
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

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("metadata",
                            "Analyze the provided inputs and generate structured metadata representing "
                            "them.",
                            &execute_metadata, &render_help_metadata);

} // namespace prompt::prompts
