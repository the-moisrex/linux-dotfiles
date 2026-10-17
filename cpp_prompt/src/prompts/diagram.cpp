#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_diagram(prompt_context& ctx) {
    // diagram.sh runs init_prompt (which already validated --head) before its
    // own --type loop, so --head errors win.
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    // diagram.sh's loop walks the ORIGINAL arguments, not the parsed ARGS.
    std::string diagram_type = "mermaid";
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--type" || ctx.args[i] == "-t") {
            if (i + 1 >= ctx.args_count) {
                throw usage_error{"Missing value for --type\n"};
            }
            diagram_type = std::string(ctx.args[i + 1]);
            ++i;
        }
    }

    std::string output;
    output += "You are a software architecture diagramming expert.\nGenerate a clear, accurate diagram from the "
              "provided code or description.\n\n";
    output += std::format("Output format: {}\n", diagram_type);
    output += "\nGuidelines:\n- Capture the essential relationships, not every detail\n- Use meaningful labels on all "
              "nodes and edges\n- Group related components together\n- For sequence diagrams, show the most important "
              "interactions\n- For class diagrams, show inheritance, composition, and key methods\n- For flowcharts, "
              "show decision points and error paths\n- Include a brief legend or title if the diagram is complex\n\n";
    output += "If the code is complex, produce multiple focused diagrams rather than one "
              "overwhelming diagram.\n";
    output += '\n';

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += std::format("File: {}\n\n", file.filename().string());
            output += std::format("```{}\n", infer_lang(file));
            output += trim_context_nl(read_file(file), head_lines);
            // diagram.sh leaves a blank line before the closing fence.
            output += "\n```\n\n";
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_diagram(std::ostream& os) {
    os << R"EOF(Usage: prompt diagram [--head N] [--type TYPE] [FILE...]
       echo "task description" | prompt diagram [--head N] [--type TYPE] [FILE...]

Generates code diagrams (Mermaid, PlantUML, ASCII) from source code or descriptions.

Options:
  --head N       Keep only the first N lines of the embedded context
  --type, -t     Diagram type: mermaid (default), plantuml, ascii, flowchart, sequence, class
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("diagram",
                            "Generates code diagrams (Mermaid, PlantUML, ASCII) from source code or "
                            "descriptions.",
                            &execute_diagram, &render_help_diagram);

} // namespace prompt::prompts
