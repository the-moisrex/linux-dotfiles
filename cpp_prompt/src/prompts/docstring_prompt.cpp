#include "prompt/prompts/docstring_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <unistd.h>

namespace prompt::prompts {

prompt_result execute_docstring(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Add comprehensive docstrings to all functions, classes, and methods in the "
              "following code.\n";
    output += "Ensure the docstrings clearly describe:\n";
    output += "1. The overall purpose and behavior.\n";
    output += "2. The parameters (including their expected types).\n";
    output += "3. The return values (and types).\n";
    output += "4. Any exceptions or errors that might be raised.\n";
    output += "5. Don't explain trivial or add useless information.\n";
    output += "Adhere to standard documentation conventions for the specific language if they "
              "exist (e.g., PEP 257/Google style for Python, JSDoc for JavaScript/TypeScript, "
              "JavaDoc for Java).\n";
    output += "Return the updated code with the newly added docstrings or write a patch file.\n";
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        auto resolved = ctx.resolve_input_file(ctx.args[i]);
        if (!resolved) {
            error += "prompt docstring: file not found: " + std::string(ctx.args[i]) + "\n";
            continue;
        }
        if (::access(resolved->c_str(), R_OK) != 0) {
            error += "prompt docstring: file not readable: \"" + std::string(ctx.args[i]) + "\"; resolved to \"" +
                     resolved->string() + "\"\n";
            continue;
        }
        output += "File: " + resolved->filename().string() + "\n\n";
        output += "```" + infer_lang(*resolved) + "\n";
        output += trim_context_nl(read_file(*resolved), head_lines);
        output += "\n```\n";
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_docstring(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt docstring [--head N] [FILE...]
       some-command | prompt docstring [--head N] [FILE...]

Asks the AI to add or improve docstrings for functions, classes, and methods in the provided code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
