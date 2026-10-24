#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <unistd.h>

namespace prompt::prompts {

static void execute_docstring(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output =
        R"PROMPT(Add comprehensive docstrings to all functions, classes, and methods in the following code.
Ensure the docstrings clearly describe:
1. The overall purpose and behavior.
2. The parameters (including their expected types).
3. The return values (and types).
4. Any exceptions or errors that might be raised.
5. Don't explain trivial or add useless information.
Adhere to standard documentation conventions for the specific language if they exist (e.g., PEP 257/Google style for Python, JSDoc for JavaScript/TypeScript, JavaDoc for Java).
Return the updated code with the newly added docstrings or write a patch file.

)PROMPT";

    std::string error;
    for (auto const& arg : positional_args(v)) {
        auto resolved = resolve_input_file(v.env, arg);
        if (!resolved) {
            error += std::format("prompt docstring: file not found: {}\n", arg);
            continue;
        }
        if (::access(resolved->c_str(), R_OK) != 0) {
            error += "prompt docstring: file not readable: \"" + std::string(arg) + "\"; resolved to \"" +
                     resolved->string() + "\"\n";
            continue;
        }
        output += std::format("File: {}\n\n```{}\n", resolved->filename().string(), infer_lang(*resolved));
        output += number_lines(trim_context_nl(read_file(*resolved), opts.head_lines), opts.numbered);
        output += "\n```\n";
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_docstring(std::ostream& os) {
    os << file_usage_help("docstring") << R"EOF(
Asks the AI to add or improve docstrings for functions, classes, and methods in the provided code.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("docstring",
                            "Asks the AI to add or improve docstrings for functions, classes, and methods "
                            "in the provided code.",
                            &execute_docstring, &render_help_docstring);

} // namespace prompt::prompts
