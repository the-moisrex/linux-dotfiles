#include "prompt/prompts/man_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_man(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
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
        std::ostringstream help_os;
        render_help_man(help_os);
        return {std::string{}, 1, false, "prompt man: no manual page provided\n" + help_os.str()};
    }

    std::string man_pages;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (i) man_pages += ' ';
        man_pages += pages[i];
    }

    // man.sh: man -P cat "${ARGS[@]}" 2>&1 — stderr is folded into the capture
    // so a missing page still reports its own error inside the code block.
    std::vector<std::string> storage;
    storage.push_back("bash");
    storage.push_back("-c");
    storage.push_back("man -P cat \"$@\" 2>&1");
    storage.push_back("man");
    for (auto const& p : pages) storage.push_back(p);

    std::vector<char const*> argv;
    argv.reserve(storage.size() + 1);
    for (auto const& s : storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);

    auto result = prompt::process::run_command(argv);

    std::string output;
    output += "Manual page for `" + man_pages + "`:\n\n";
    if (result.exit_code != 0) {
        output += "(Exited with status: " + std::to_string(result.exit_code) + ")\n\n";
    }
    output += "```text\n";
    std::string stripped = result.stdout_data;
    while (!stripped.empty() && stripped.back() == '\n') stripped.pop_back();
    if (!stripped.empty()) {
        output += trim_context_nl(result.stdout_data, head_lines);
    }
    output += "\n```\n\n";

    return {std::move(output), 0, false, std::string{}};
}

void render_help_man(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt man [--head N] <page>...
       some-input | prompt man [--head N] <page>...

Fetches the manual page for the given command(s) and appends it as a Markdown code block.
This is essentially a shorthand for `prompt cli man -P cat <page>`.

Options:
  --head N   Keep only the first N lines of the man page output
)EOF";
}

} // namespace prompt::prompts
