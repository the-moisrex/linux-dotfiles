#include "../core/fs.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

prompt_output execute_files(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::vector<std::string_view> file_args;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
        } else {
            file_args.push_back(ctx.args[i]);
        }
    }

    std::vector<std::filesystem::path> selected_files;
    std::string errors;
    if (file_args.empty()) {
        selected_files = ctx.select_files();
    } else {
        for (auto arg : file_args) {
            auto resolved = ctx.resolve_input_file(arg);
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
            if (head_lines > 0) {
                output += std::format("File {} (first {} lines)\n\n", rel.string(), std::to_string(head_lines));
            } else {
                output += std::format("File {}\n\n", rel.string());
            }
            output += std::format("```{}\n", lang);
            output += trim_context(content, head_lines);
            output += "\n```\n\n";
        }
    }

    if (!errors.empty()) return {std::move(output), std::move(errors), false, false};
    return {std::move(output), std::string{}, false, false};
}

void render_help_files(std::ostream& os) {
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

} // namespace prompt::prompts