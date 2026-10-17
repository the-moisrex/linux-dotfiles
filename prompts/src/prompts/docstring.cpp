#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <unistd.h>

namespace prompt::prompts {

static void execute_docstring(prompt_context& ctx) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Add comprehensive docstrings to all functions, classes, and methods in the "
              "following code.\n";
    output += "Ensure the docstrings clearly describe:\n1. The overall purpose and behavior.\n2. The parameters "
              "(including their expected types).\n3. The return values (and types).\n4. Any exceptions or errors that "
              "might be raised.\n5. Don't explain trivial or add useless information.\n";
    output += "Adhere to standard documentation conventions for the specific language if they "
              "exist (e.g., PEP 257/Google style for Python, JSDoc for JavaScript/TypeScript, "
              "JavaDoc for Java).\n";
    output += "Return the updated code with the newly added docstrings or write a patch file.\n\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        auto resolved = ctx.resolve_input_file(ctx.args[i]);
        if (!resolved) {
            error += std::format("prompt docstring: file not found: {}\n", std::string(ctx.args[i]));
            continue;
        }
        if (::access(resolved->c_str(), R_OK) != 0) {
            error += "prompt docstring: file not readable: \"" + std::string(ctx.args[i]) + "\"; resolved to \"" +
                     resolved->string() + "\"\n";
            continue;
        }
        output += std::format("File: {}\n\n", resolved->filename().string());
        output += std::format("```{}\n", infer_lang(*resolved));
        output += trim_context_nl(read_file(*resolved), head_lines);
        output += "\n```\n";
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_docstring(std::ostream& os) {
    os << R"EOF(Usage: prompt docstring [--head N] [FILE...]
       some-command | prompt docstring [--head N] [FILE...]

Asks the AI to add or improve docstrings for functions, classes, and methods in the provided code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("docstring",
                            "Asks the AI to add or improve docstrings for functions, classes, and methods "
                            "in the provided code.",
                            &execute_docstring, &render_help_docstring);

} // namespace prompt::prompts
