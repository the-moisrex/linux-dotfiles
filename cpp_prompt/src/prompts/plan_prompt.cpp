#include "prompt/prompts/plan_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

// plan.sh embeds through _common.sh's embed_file, whose shape differs from the
// inline loops the other prompts use (leading blank line, no trailing one).
std::string embed_file_block(std::filesystem::path const& path, std::size_t head_lines) noexcept {
    if (!std::filesystem::is_regular_file(path)) return {};

    std::string name = path.filename().string();
    std::string output;
    output += "\n";
    output += "File: " + name + "\n";
    output += "```" + infer_lang(path) + "\n";
    output += trim_context_nl(read_file(path), head_lines);
    output += "```\n";
    return output;
}

} // namespace

prompt_result execute_plan(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += R"HELP(You are in PLAN MODE. Your goal is to produce a detailed, decision-complete
implementation plan before writing any code.

Work through these phases in order. Ask the user questions at any point when
something is unclear or you need more context.

## Phase 1: Understand

- Ask the user to describe the goal and paste any relevant code, configs,
  error messages, or file contents.
- Do not guess what the code looks like. If you need to see specific files,
  ask the user to paste them.
- Summarize what you understand so far and confirm with the user.

## Phase 2: Clarify

- Ask specific, targeted questions about requirements, constraints, edge
  cases, and success criteria.
- Identify any missing context: Which languages, frameworks, or tools are
  in use? What are the compatibility targets?
- Resolve ambiguities before moving on.

## Phase 3: Explore

- Analyze the code the user provided. Identify patterns, conventions,
  naming styles, and existing abstractions.
- Note any related areas that may be affected but were not provided.
  Ask the user to paste those if needed.
- Identify risks, dependencies, and potential breaking changes.

## Phase 4: Design

- Consider at least two viable approaches. Briefly explain the tradeoffs
  of each.
- Choose one approach and justify the choice.
- State any assumptions you are making.

## Phase 5: Plan

Produce a structured plan with these sections:

- **Goal** - What we are building and why
- **Success criteria** - How we will know it is done
- **Approach** - The chosen design and why
- **Alternatives considered** - Other options and why they were not chosen
- **Implementation steps** - Concrete, ordered steps with enough detail to
  execute without further questions
- **Files to change** - Specific files and what changes they need
- **Risks** - What could go wrong and how to mitigate it
- **Test plan** - How to verify correctness
- **Assumptions** - What you are assuming (and what needs confirmation)

## Rules

- Do NOT write any code or make changes yet.
- Ask the user to paste code rather than assuming you can access files.
- Be specific: reference file names, function names, and line numbers when
  discussing existing code.
- If a step in the plan is unclear, flag it instead of guessing.
- Keep the plan concise enough to scan quickly but detailed enough to
  execute without further questions.
)HELP";
    // The heredoc ends with a blank line before its terminator.
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        // plan.sh: resolve_input_file first, then embed_file on the result.
        auto resolved = ctx.resolve_input_file(ctx.args[i]);
        std::filesystem::path file(ctx.args[i]);
        if (resolved && std::filesystem::is_regular_file(*resolved)) {
            output += embed_file_block(*resolved, head_lines);
        } else if (std::filesystem::is_regular_file(file)) {
            output += embed_file_block(file, head_lines);
        } else {
            error += "Warning: File not found: " + std::string(ctx.args[i]) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_plan(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt plan [--head N] [FILE...]
       echo "task description" | prompt plan [--head N] [FILE...]

Generates a structured plan mode prompt for an AI assistant.
The AI will be instructed to ask clarifying questions, analyze provided code,
and produce a detailed implementation plan before writing any code.
Designed for chat mode where the AI cannot access files directly.

Use this when you want the AI to plan before implementing.

Options:
  --head N   Keep only the first N lines of embedded context files
)EOF";
}

} // namespace prompt::prompts
