#include "stage.hpp"
#include "prompt_registry.hpp"
#include <utility>

namespace prompt {

void stdin_source::load(std::string content) {
    content_ = std::move(content);
    piped_ = true;
    spent_ = false;
}

std::string clipboard_content(environment const& env) {
    return (env.clipboard_content != nullptr) ? env.clipboard_content() : std::string{};
}

std::optional<std::string> clipboard_identifier(environment const& env) {
    return (env.clipboard_identifier != nullptr) ? env.clipboard_identifier() : std::nullopt;
}

std::vector<std::filesystem::path> select_files(environment const& env) {
    return (env.select_files != nullptr) ? env.select_files() : std::vector<std::filesystem::path>{};
}

std::optional<std::filesystem::path> resolve_input_file(environment const& env, std::string_view wanted) {
    return (env.resolve_input_file != nullptr) ? env.resolve_input_file(wanted) : std::nullopt;
}

result run_sub_prompt(view const& parent, std::string_view name, std::span<std::string_view const> args,
                      std::optional<std::string_view> stdin_text, facts const* extra_facts) {
    auto const* desc = find_prompt(name);
    if (desc == nullptr) throw prompt_error{3, "prompt: prompt '" + std::string(name) + "' not found\n"};

    facts prior = parent.prior;
    if (extra_facts != nullptr) prior.merge(*extra_facts, true);

    stdin_source in;
    if (stdin_text) in.load(std::string(*stdin_text));

    result out;
    try {
        view const child{
            .name = desc->name,
            .args = args,
            .text = {},
            .prior = prior,
            .env = parent.env,
            .in = in,
        };
        desc->execute_fn(child, out);
    } catch (prompt_error const& err) {
        // The child's own stdin source is gone with the frame; report whether
        // it was spent so `auto` can mark the chain's pipe consumed.
        throw prompt_error{err.exit_code(), err.what(), err.partial_output(), err.no_clipboard(), in.spent()};
    }
    return out;
}

} // namespace prompt
