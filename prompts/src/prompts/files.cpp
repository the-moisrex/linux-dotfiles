#include "../core/fs.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

static void execute_files(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    auto file_args = positional_args(ctx);

    std::vector<std::filesystem::path> selected_files;
    std::string errors;
    if (file_args.empty()) {
        selected_files = select_files(ctx);
    } else {
        for (auto const& arg : file_args) {
            auto resolved = resolve_input_file(ctx, arg);
            if (!resolved) {
                errors += std::format("prompt files: file not found: {}\n", std::string(arg));
                continue;
            }
            if (::access(resolved->c_str(), R_OK) != 0) {
                errors += "prompt files: file not readable: \"" + std::string(arg) + "\"; resolved to \"" +
                          resolved->string() + "\"\n";
                continue;
            }
            selected_files.push_back(*resolved);
        }
    }

    std::string output;

    for (auto const& file : selected_files) {
        if (!std::filesystem::exists(file) || !std::filesystem::is_regular_file(file)) continue;

        auto rel = fs::relative_path(file, ctx.git_root.empty() ? std::nullopt : std::optional(ctx.git_root));
        auto lang = infer_lang(file);
        auto content = read_file(file);

        if (!content.empty()) {
            if (head_lines) {
                output += std::format("File {} (first {} lines)\n\n", rel.string(), std::to_string(*head_lines));
            } else {
                output += std::format("File {}\n\n", rel.string());
            }
            output += std::format("```{}\n", lang);
            output += trim_context(content, head_lines);
            output += "\n```\n\n";
        }
    }

    if (!errors.empty()) {
        ctx.err = std::move(errors);
    }
    ctx.out = std::move(output);
}

static void render_help_files(std::ostream& os) {
    os << R"EOF(Usage: prompt files [--head N] [FILE...]
       some-command | prompt files [--head N] [FILE...]

Appends the given fuzzily found files as Markdown code blocks.
If you're inside a Git repository, file headings are printed relative to the
repository root.
If no files are provided, `fzf -m` is used to choose them interactively.

Options:
  --head N   Keep only the first N lines of each embedded file
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt(
    "files",
    "Appends the given fuzzily found files as Markdown code blocks. If you're inside a Git "
    "repository, file headings are printed relative to the repository root. If no files are provided, "
    "`fzf -m` is used to choose them interactively.",
    &execute_files, &render_help_files);

} // namespace prompt::prompts