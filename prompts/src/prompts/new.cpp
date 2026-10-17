#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include "prompt_assets.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace prompt::prompts {

namespace {

// The fenced-block form the native prompts use for embedded context.
std::string embed_text(std::string_view content, std::string_view lang, std::string_view label,
                       std::optional<std::size_t> head_lines) {
    std::string out;
    out += std::format("\nFile: {}\n", std::string(label));
    out += std::format("```{}\n", std::string(lang));
    out += trim_context_nl(content, head_lines);
    out += "```\n";
    return out;
}

std::string embed_block(std::filesystem::path const& path, std::optional<std::size_t> head_lines) {
    if (!std::filesystem::is_regular_file(path)) return {};
    return embed_text(read_file(path), infer_lang(path), path.filename().string(), head_lines);
}

} // namespace

static void execute_new(prompt_context& ctx) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    if (ctx.stdin_consumed) {
        std::string content = ctx.stdin_content;
        while (!content.empty() && content.back() == '\n') content.pop_back();
        if (!content.empty()) output += content + "\n\n";
    }

    output += R"NEWTEXT(Write a new native (C++) prompt for the prompt dispatcher in `prompts/`.

Goal:
Create a complete, ready-to-use implementation in `prompts/src/prompts/<name>.cpp` that generates an AI prompt.
The native dispatcher in `prompts/` is a one-file-per-prompt C++ replacement for the bash `prompts-shell/` (its `prompt.sh` dispatcher + `*.sh` prompts); match the style, structure, and helpers of the prompts already there.

Requirements for the generated prompt:
- Implement `static void execute_<name>(prompt_context&)` and `static void render_help_<name>(std::ostream&)`, then register the prompt at the end of the file:
    bool const registered = prompt::register_prompt("<name>", "<summary>", &execute_<name>, &render_help_<name>);
  CMake globs src/prompts/*.cpp, so the file registers itself and no other file changes.
- "<summary>" is the first paragraph of render_help_<name>'s help (verbatim), and it is what `prompt list` prints; it must read as a complete standalone summary.
- The prompt is invoked as `prompt <name> ...`, so the help's Usage line must say `Usage: prompt <name>`.
- Write the result onto the prompt_context handed in: ctx.out for stdout (it is sanitised, piped onward and copied to the clipboard), ctx.err for stderr (the exit status stays 0). Set ctx.no_clipboard = true to opt this output out of the clipboard.
- Report failures by throwing usage_error (exit 2) or soft_error (exit 1) with a message ending in a newline; never call std::exit and never return a non-zero status directly.
- Accept `--head N` with parse_head_option and pass the result to the embedding helpers; embed files with embed_file_block (or trim_context_nl + infer_lang) and stdin with embed_stdin when ctx.stdin_consumed.
- Locate repository tools with prompt::fs::bin_tool and run subprocesses with prompt::process::command / run_command (argv arrays, no shell quoting).
- Keep the prompt focused, minimal, and consistent with the prompts already in src/prompts/.
- Do not invent SDK helpers that are not in the references embedded below.

If a task description appears above or below, implement exactly that prompt behaviour.
If extra reference files are provided, treat them as examples or domain context for the new prompt.

Return:
1. A short explanation of the design choices.
2. The full contents of `prompts/src/prompts/<name>.cpp`.
3. The <name> you chose and one or two example invocations.
)NEWTEXT";

    for (auto const& asset : prompt::prompt_assets::assets) {
        output += embed_text(asset.text, asset.lang, asset.label, head_lines);
    }

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::string_view wanted = ctx.args[i];
        std::filesystem::path resolved;
        if (auto found = ctx.resolve_input_file(wanted)) {
            resolved = *found;
        }
        if (!resolved.empty() && std::filesystem::is_regular_file(resolved)) {
            output += embed_block(resolved, head_lines);
        } else if (std::filesystem::is_regular_file(std::filesystem::path(wanted))) {
            output += embed_block(std::filesystem::path(wanted), head_lines);
        } else {
            error += std::format("Warning: File not found or is not a regular file: {}\n", std::string(wanted));
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_new(std::ostream& os) {
    os << R"EOF(Usage: prompt new [--head N] [FILE...]
       echo "what the prompt should do" | prompt new [--head N] [FILE...]

Builds a prompt that asks an AI to write a new native (C++) prompt for the `prompts/` dispatcher following its conventions.

Always embeds the SDK headers and an example prompt so the AI has enough
context to generate a good, consistent prompt.

Provide the desired prompt behavior on stdin and/or extra reference
files as arguments.

Options:
  --head N   Keep only the first N lines of each embedded context file
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("new",
                                                "Builds a prompt that asks an AI to write a new native (C++) prompt "
                                                "for the `prompts/` dispatcher following its conventions.",
                                                &execute_new, &render_help_new);

} // namespace prompt::prompts
