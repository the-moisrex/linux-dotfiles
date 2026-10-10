#include "prompt/prompts/new_prompt.hpp"
#include "prompt/legacy/legacy_runner.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace prompt::prompts {

namespace {

// Port of _common.sh embed_file: a leading blank line, the heading, the
// fenced body and the closing fence (no blank line after it).
std::string embed_block(std::filesystem::path const& path, std::string_view label, std::size_t head_lines) noexcept {
    if (!std::filesystem::is_regular_file(path)) return {};
    std::string name = path.filename().string();
    std::string heading = label.empty() ? name : std::string(label);
    std::string out;
    out += "\nFile: " + heading + "\n";
    out += "```" + infer_lang(path) + "\n";
    out += trim_context_nl(read_file(path), head_lines);
    out += "```\n";
    return out;
}

} // namespace

prompt_result execute_new(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    if (ctx.stdin_consumed) {
        std::string content = ctx.stdin_content;
        while (!content.empty() && content.back() == '\n') content.pop_back();
        if (!content.empty()) output += content + "\n\n";
    }

    output += R"NEWTEXT(Write a new bash prompt script for this repository.

Goal:
Create a complete, ready-to-use script under prompts/ that generates an AI prompt.
Match the style, structure, and helpers used by the existing prompt scripts.

Requirements for the generated script:
- Start with a proper bash shebang.
- Define a show_help function and support --help/-h via the shared argument parser.
- The first paragraph of show_help must be a complete standalone summary: it is what 'prompt list' shows.
- The script will be called using a wrapper script named 'prompt' (e.g., 'prompt my-script'). Ensure the help menu usage reflects this (e.g., 'Usage: prompt my-script').
- Source prompts/_common.sh and call init_prompt (or init_prompt --no-files).
- If files are needed, call get_files || true after init_prompt.
- If stdin content is needed, call embed_stdin || true (or read_stdin for raw access).
- Use infer_lang and trim_context when embedding file contents.
- Print clear AI instructions first, then embed any needed context as fenced code blocks.
- Prefer actionable output from the AI (for example tables, checklists, or a git diff) when that fits the task.
- Keep the script focused, minimal, and consistent with repository conventions.
- Do not invent APIs that are not present in _common.sh unless they are pure local helpers.

If a task description appears above or below, implement exactly that prompt behavior.
If extra reference files are provided, treat them as examples or domain context for the new prompt.

Return:
1. A short explanation of the design choices.
2. The full bash script contents.
3. A suggested filename under prompts/.
4. One or two example invocations.
)NEWTEXT";

    auto dir = legacy::prompts_dir();
    output += embed_block(dir / "_common.sh", "prompts/_common.sh", head_lines);
    output += embed_block(dir / "fix.sh", "prompts/fix.sh (example)", head_lines);
    output += embed_block(dir / "symbols.sh", "prompts/symbols.sh (example)", head_lines);

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
            output += embed_block(resolved, {}, head_lines);
        } else if (std::filesystem::is_regular_file(std::filesystem::path(wanted))) {
            output += embed_block(std::filesystem::path(wanted), {}, head_lines);
        } else {
            error += "Warning: File not found or is not a regular file: " + std::string(wanted) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error), ctx.stdin_consumed};
}

void render_help_new(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt new [--head N] [FILE...]
       echo "what the prompt should do" | prompt new [--head N] [FILE...]

Builds a prompt that asks an AI to write a new bash prompt script following this repository's prompt conventions.

Always embeds shared infrastructure and example prompt scripts so the AI
has enough context to generate a good, consistent prompt script.

Provide the desired prompt behavior on stdin and/or extra reference
files as arguments.

Options:
  --head N   Keep only the first N lines of each embedded context file
)EOF";
}

} // namespace prompt::prompts
