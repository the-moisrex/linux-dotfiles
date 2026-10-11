#include "prompt/prompts/diagram_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_diagram(prompt_context&& ctx) noexcept {
    // diagram.sh runs init_prompt (which already validated --head) before its
    // own --type loop, so --head errors win.
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    // diagram.sh's loop walks the ORIGINAL arguments, not the parsed ARGS.
    std::string diagram_type = "mermaid";
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--type" || ctx.args[i] == "-t") {
            if (i + 1 >= ctx.args_count) {
                return {std::string{}, 2, false, "Missing value for --type\n"};
            }
            diagram_type = std::string(ctx.args[i + 1]);
            ++i;
        }
    }

    std::string output;
    output += "You are a software architecture diagramming expert.\n";
    output += "Generate a clear, accurate diagram from the provided code or description.\n";
    output += "\n";
    output += "Output format: " + diagram_type + "\n";
    output += "\n";
    output += "Guidelines:\n";
    output += "- Capture the essential relationships, not every detail\n";
    output += "- Use meaningful labels on all nodes and edges\n";
    output += "- Group related components together\n";
    output += "- For sequence diagrams, show the most important interactions\n";
    output += "- For class diagrams, show inheritance, composition, and key methods\n";
    output += "- For flowcharts, show decision points and error paths\n";
    output += "- Include a brief legend or title if the diagram is complex\n";
    output += "\n";
    output += "If the code is complex, produce multiple focused diagrams rather than one "
              "overwhelming diagram.\n";
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            // diagram.sh leaves a blank line before the closing fence.
            output += "\n```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_diagram(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt diagram [--head N] [--type TYPE] [FILE...]
       echo "task description" | prompt diagram [--head N] [--type TYPE] [FILE...]

Generates code diagrams (Mermaid, PlantUML, ASCII) from source code or descriptions.

Options:
  --head N       Keep only the first N lines of the embedded context
  --type, -t     Diagram type: mermaid (default), plantuml, ascii, flowchart, sequence, class
)EOF";
}

} // namespace prompt::prompts
