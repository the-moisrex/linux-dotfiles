#include "prompt/prompts/fix_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_fix(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    std::vector<std::string_view> files;
    
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head" && i + 1 < ctx.args_count) {
            head_lines = std::stoull(std::string(ctx.args[++i]));
        } else {
            files.push_back(ctx.args[i]);
        }
    }
    
    std::string output;
    output += "Find the root problem here and propose the smallest useful fix.\n";
    output += "Explain the issue briefly, mention any important assumptions, and provide the answer primarily as a git diff that can be applied directly.\n";
    output += "Prefer minimal, surgical changes over broad rewrites.\n\n";
    
    if (ctx.stdin_consumed && !ctx.stdin_content.empty()) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }
    
    for (auto file_sv : files) {
        std::filesystem::path file(file_sv);
        if (std::filesystem::exists(file)) {
            output += embed_file(file, file.filename().string(), head_lines);
        }
    }
    
    return {std::move(output), 0, false};
}

void render_help_fix(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt fix [--head N] [FILE...]
       some-command | prompt fix [--head N] [FILE...]

Find the root problem here and propose the smallest useful fix.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts