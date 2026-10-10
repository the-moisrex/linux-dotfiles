#include "prompt/prompts/tse_find_prompt.hpp"
#include "prompt/legacy/legacy_runner.hpp"
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_tse_find(prompt_context&& ctx) noexcept {
    // tse.find.sh mixes the chat/agentic generator, the .tse snapshot review
    // and the live-screen review, each with its own python helpers — the
    // script stays the single source of truth, so run it like the dispatcher
    // does instead of re-implementing the flow here.
    std::vector<std::string> storage;
    storage.reserve(ctx.args_count);
    for (std::size_t i = 0; i < ctx.args_count; ++i) storage.emplace_back(ctx.args[i]);

    std::vector<char const*> argv;
    argv.reserve(storage.size() + 1);
    for (auto const& s : storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);

    auto result = legacy::run_prompt_script("tse.find", ctx.stdin_content, argv);
    // tse.find.sh calls read_stdin, so a piped stdin is spent here.
    return {result.stdout_data, result.exit_code, result.no_clipboard, result.stderr_data, ctx.stdin_consumed};
}

void render_help_tse_find(std::ostream& os) noexcept { os << legacy::help_text("tse.find"); }

} // namespace prompt::prompts
