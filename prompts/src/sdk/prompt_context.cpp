#include "prompt_context.hpp"
#include "../core/clipboard.hpp"
#include "../core/fs.hpp"

namespace prompt {

void clear_output(prompt_context& ctx) {
    ctx.out.clear();
    ctx.err.clear();
    ctx.stderr_buffer.clear();
    ctx.save_hint.clear();
    ctx.no_clipboard = false;
    ctx.clear_context = false;
    ctx.stdout_streamed = false;
    ctx.stderr_streamed = false;
}

std::string clipboard_content(prompt_context const& ctx) {
    return (ctx.clipboard_content_fn != nullptr) ? ctx.clipboard_content_fn() : "";
}

std::optional<std::string> clipboard_identifier(prompt_context const& ctx) {
    return (ctx.clipboard_identifier_fn != nullptr) ? ctx.clipboard_identifier_fn() : std::nullopt;
}

std::vector<std::filesystem::path> select_files(prompt_context const& ctx) {
    return (ctx.select_files_fn != nullptr) ? ctx.select_files_fn() : std::vector<std::filesystem::path>{};
}

std::optional<std::filesystem::path> resolve_input_file(prompt_context const& ctx, std::string_view wanted) {
    return (ctx.resolve_input_file_fn != nullptr) ? ctx.resolve_input_file_fn(wanted) : std::nullopt;
}

prompt_context make_base_context() {
    prompt_context ctx;
    ctx.git_root = fs::find_git_root().value_or(std::filesystem::path{});
    ctx.exe_path = fs::self_path().value_or(std::filesystem::path{});
    return ctx;
}

} // namespace prompt