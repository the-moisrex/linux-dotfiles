#include "prompt/prompts/symbols_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_symbols(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Review the symbols in this code and find bad names that should be renamed.\n";
    output += "Focus on unclear, misleading, overly abbreviated, inconsistent, or non-idiomatic symbol "
              "names.\n";
    output += "For each rename suggestion, provide the current name, the proposed new name, and the reason "
              "in a table.\n";
    output += "Only suggest renames that materially improve readability or maintainability.\n";

    // symbols.sh tests $#, which is the script's whole argument list (bash's
    // parse_arguments shifts the function's own parameters, not the caller's
    // ones) — so even a bare `--head N` selects the "files" wording.
    if (ctx.args_count > 0) {
        output += "At the end, provide a git diff that applies the renames in the provided files.\n";
    } else {
        output += "At the end, provide a git diff that applies the renames.\n";
    }

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += "\nFile: " + file.filename().string() + "\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            output += "\n```\n";
        } else {
            error += "Warning: File not found or is not a regular file: " + std::string(ctx.args[i]) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_symbols(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt symbols [--head N] [file...]

Builds a prompt that reviews symbol names.
If file paths are given, it reads those files; otherwise it reads stdin.
Example: `prompt symbols $(fzf)`

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
