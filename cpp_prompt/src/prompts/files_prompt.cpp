#include "prompt/prompts/files_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_files(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    std::vector<std::string_view> file_args;
    
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head" && i + 1 < ctx.args_count) {
            head_lines = std::stoull(std::string(ctx.args[++i]));
        } else {
            file_args.push_back(ctx.args[i]);
        }
    }
    
    std::vector<std::filesystem::path> selected_files;
    if (file_args.empty()) {
        selected_files = ctx.select_files();
    } else {
        for (auto arg : file_args) {
            if (auto resolved = ctx.resolve_input_file(arg)) {
                selected_files.push_back(*resolved);
            }
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
                output += "File " + rel.string() + " (first " + std::to_string(head_lines) + " lines)\n\n";
            } else {
                output += "File " + rel.string() + "\n\n";
            }
            output += "```" + lang + "\n";
            output += trim_context(content, head_lines);
            output += "\n```\n\n";
        }
    }
    
    return {std::move(output), 0, false};
}

void render_help_files(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt files [--head N] [FILE...]
       some-command | prompt files [--head N] [FILE...]

Appends the given fuzzily found files as Markdown code blocks.
If you're inside a Git repository, file headings are printed relative to the
repository root.
If no files are provided, fzf -m is used to choose them interactively.

Options:
  --head N   Keep only the first N lines of each embedded file
)EOF";
}

} // namespace prompt::prompts