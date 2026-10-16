#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace prompt::prompts {

static void render_help_man(std::ostream& os);

static void execute_man(prompt_context& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::vector<std::string> pages;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        pages.emplace_back(ctx.args[i]);
    }

    if (pages.empty()) {
        std::string help_os = help_text(render_help_man);
        throw soft_error{"prompt man: no manual page provided\n" + help_os};
    }

    std::string man_pages;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (i != 0U) man_pages += ' ';
        man_pages += pages[i];
    }

    // man.sh: man -P cat "${ARGS[@]}" 2>&1 — stderr is folded into the capture
    // so a missing page still reports its own error inside the code block.
    std::vector<std::string> storage = {"bash", "-c", "man -P cat \"$@\" 2>&1", "man"};
    for (auto const& p : pages) storage.push_back(p);

    auto result = prompt::process::run_command(storage);

    std::string output;
    output += std::format("Manual page for `{}`:\n\n", man_pages);
    if (result.exit_code != 0) {
        output += std::format("(Exited with status: {})\n\n", std::to_string(result.exit_code));
    }
    output += "```text\n";
    std::string stripped = result.stdout_data;
    while (!stripped.empty() && stripped.back() == '\n') stripped.pop_back();
    if (!stripped.empty()) {
        output += trim_context_nl(result.stdout_data, head_lines);
    }
    output += "\n```\n\n";

    ctx.out = std::move(output);
}

static void render_help_man(std::ostream& os) {
    os << R"EOF(Usage: prompt man [--head N] <page>...
       some-input | prompt man [--head N] <page>...

Fetches the manual page for the given command(s) and appends it as a Markdown code block.
This is essentially a shorthand for `prompt cli man -P cat <page>`.

Options:
  --head N   Keep only the first N lines of the man page output
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("man",
                            "Fetches the manual page for the given command(s) and appends it as a Markdown code "
                            "block. This is essentially a shorthand for `prompt cli man -P cat <page>`.",
                            &execute_man, &render_help_man);

} // namespace prompt::prompts
