#include "prompt/prompts/ask_for_help_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/prompt_registry.hpp"

#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prompt::prompts {

namespace {

// Prompts the local AI can handle itself, or meta prompts: no need to ask the
// bigger AI about these. Mirrors the awk `$1 !~ excl` exclusion where the
// pattern is fully anchored, i.e. an exact-name set.
bool is_excluded(std::string const& name) noexcept {
    static const std::set<std::string> excluded = {
        "english", "farsi", "auto",         "summarize",    "metadata", "tweets", "diagram",
        "explain", "list",  "list-prompts", "ask-for-help", "ai-said",  "yt",
    };
    return excluded.count(name) > 0;
}

// Locate the prompts/ directory whose list.sh the bash prompt runs (its own
// COMMON_DIR, i.e. <repo>/prompts). Robust to where the binary lives: git
// root first, then a cwd walk, then the exe-relative search_dirs.
// Builds the "- `name` — desc" catalog from the native registry (sorted with
// the same collation the bash list prompt applies). The registry is the
// complete prompt catalog, so no prompts/ directory is needed.
std::string build_prompt_list() noexcept {
    auto prompts = prompt::get_all_prompts();
    std::vector<prompt_descriptor const*> sorted;
    sorted.reserve(prompts.size());
    for (auto const& desc : prompts) sorted.push_back(&desc);
    std::sort(sorted.begin(), sorted.end(), prompt_descriptor_less);

    std::string out;
    for (auto const* desc : sorted) {
        if (is_excluded(std::string(desc->name))) continue;
        out += "- `";
        out += std::string(desc->name);
        out += "` \xE2\x80\x94 ";
        out += desc->help_summary.empty() ? std::string("(no help)") : std::string(desc->help_summary);
        out += '\n';
    }
    return out;
}

} // namespace

prompt_result execute_ask_for_help(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }
    (void)head_lines; // the command-construction prompt embeds no head-capped files

    std::string out;

    out += "# Available context-gathering prompts\n";
    out += "\n";
    out += "Use these with the shorthand syntax to compose your command:\n";
    out += "  .note \"text\"        Add a note or context description\n";
    out += "  .cli \"command\"      Run a shell command and embed its output\n";
    out += "  .files file1 file2  Embed file contents as fenced code blocks\n";
    out += "  .agents             Embed agent instruction files (AGENTS.md, etc.)\n";
    out += "  .git-dirty          Show uncommitted changes in the repo\n";
    out += "  .repo               Show the repository file structure\n";
    out += "  .clipboard          Embed clipboard content\n";
    out += "\n";
    out += "Example command:\n";
    out += "  prompt .note \"WIP: fixing parser bug\" .files parser.h parser.cpp .cli \"make test 2>&1\"\n";
    out += "\n";
    out += "# Other available prompts\n";
    out += "\n";
    out += "Besides the shorthand above, every prompt below can also be chained into\n";
    out += "the command (prefix it with '.', e.g. '.man ls') to add more context:\n";
    out += "\n";
    out += build_prompt_list();
    out += "\n";
    out += "---\n";
    out += "\n";
    out += "# How this works\n";
    out += "\n";
    out += "You are a dumb agentic AI. Your ONLY job is to generate a `prompt` command line.\n"
           "Do NOT try to solve the problem yourself. Do NOT research anything. Do NOT explain\n"
           "how things work. Just generate the command.\n"
           "\n"
           "The workflow is:\n"
           "1. You generate the `prompt` command below.\n"
           "2. The user runs it in their terminal.\n"
           "3. The user takes the command's output and gives it to a smarter chatbot AI.\n"
           "4. The smart AI's response is brought back to you as context to continue with.\n"
           "\n"
           "So your output is NOT the solution \xE2\x80\x94 it is the COMMAND that produces the context\n"
           "the smart AI needs.\n";
    out += "\n";
    out += "# Your task\n";
    out += "\n";
    out += "Given the problem description above, compose a `prompt` command that gathers\n"
           "all the context a smarter AI would need to solve the problem.\n"
           "\n"
           "Use the shorthand syntax:\n"
           "  prompt .note \"problem description\" .files <relevant files> .cli \"command to reproduce\"\n"
           "\n"
           "Guidelines:\n"
           "- Always start with .note describing the problem in detail.\n"
           "- Add .files for any source files, configs, or logs relevant to the problem.\n"
           "- Add .cli for commands that reproduce the error or show relevant state\n"
           "  (e.g., .cli \"make 2>&1\", .cli \"git diff\", .cli \"ls -la src/\").\n"
           "- Use .git-dirty if uncommitted changes are relevant.\n"
           "- Use .agents if the project has AGENTS.md or similar context files.\n"
           "- Use .repo if the AI needs to understand the project structure.\n"
           "\n"
           "Output ONE command in a bash code block:\n"
           "```\n"
           "prompt .note \"...\" .files ... .note \"...\" .cli \"...\" .agents .repo\n"
           "```\n"
           "\n"
           "If the problem is ambiguous, list 1-2 clarifying questions INSIDE the command for the AI.\n"
           "\n"
           "Do NOT:\n"
           "- Explain what the command does\n"
           "- Research how to solve the problem\n"
           "- Provide any solution or analysis\n"
           "- Do anything beyond generating the command\n"
           "- Give details to the AI about how to run test and build project, they don't have access to the repo\n"
           "\n"
           "Give the AI all the context and the files it needs; tell the AI about limitations, behavior requirements, "
           "any useful information or file it needs to figure it all out.\n"
           "Add enough details; the AI has no access to any files or assets because it's a Chatbot AI not agentic AI; "
           "so be careful about what the AI needs.\n"
           "Make sure to properly escape characters.\n"
           "\n";

    return {std::move(out), 0, true, std::string{}};
}

void render_help_ask_for_help(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt ask-for-help [--head N]
       echo "I'm stuck on this problem..." | prompt ask-for-help [--head N]

Generates a ready-to-run `prompt` command line that bundles all the context needed to ask another AI for help with a coding problem.

Describe your problem on stdin (or via clipboard). The AI will compose a
`prompt` command using context-gathering prompts (.note, .cli, .files, etc.)
that you can run, take the output to a smarter AI, and bring back the answer.
The output also embeds the list of other available prompts (minus translation,
summarization, and meta ones) so the AI can chain whatever context it needs.

Options:
  --head N   Keep only the first N lines of each embedded context file
)EOF";
}

} // namespace prompt::prompts
