#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_symbols(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

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
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("\nFile: {}\n", file.filename().string());
        output += std::format("```{}\n", infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
        output += "\n```\n";
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File not found or is not a regular file: {}\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_symbols(std::ostream& os) {
    os << R"EOF(Usage: prompt symbols [--head N] [file...]

Builds a prompt that reviews symbol names.
If file paths are given, it reads those files; otherwise it reads stdin.
Example: `prompt symbols $(fzf)`

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("symbols",
                            "Builds a prompt that reviews symbol names. If file paths are given, it reads those files; "
                            "otherwise it reads stdin. Example: `prompt symbols $(fzf)`",
                            &execute_symbols, &render_help_symbols);

} // namespace prompt::prompts
