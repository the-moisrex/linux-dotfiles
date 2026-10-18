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
    auto opts = file_options_or_throw(ctx);

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
    output += R"PROMPT(You are a software architecture diagramming expert.
Generate a clear, accurate diagram from the provided code or description.

)PROMPT";
    output += std::format("Output format: {}\n", diagram_type);
    output += R"PROMPT(
Guidelines:
- Capture the essential relationships, not every detail
- Use meaningful labels on all nodes and edges
- Group related components together
- For sequence diagrams, show the most important interactions
- For class diagrams, show inheritance, composition, and key methods
- For flowcharts, show decision points and error paths
- Include a brief legend or title if the diagram is complex

If the code is complex, produce multiple focused diagrams rather than one overwhelming diagram.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        // diagram.sh leaves a blank line before the closing fence.
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_diagram(std::ostream& os) {
    os << file_usage_help("diagram", "[--type TYPE] [FILE...]", "echo \"task description\"") << R"EOF(
Generates code diagrams (Mermaid, PlantUML, ASCII) from source code or descriptions.

Options:
)EOF" << file_options_help("the embedded context")
       << R"EOF(  --type, -t     Diagram type: mermaid (default), plantuml, ascii, flowchart, sequence, class
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
