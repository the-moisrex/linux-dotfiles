#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt plan [--head N] [FILE...]
       echo "task description" | prompt plan [--head N] [FILE...]

Generates a structured plan mode prompt for an AI assistant.
The AI will be instructed to ask clarifying questions, analyze provided code,
and produce a detailed implementation plan before writing any code.
Designed for chat mode where the AI cannot access files directly.

Use this when you want the AI to plan before implementing.

Options:
  --head N   Keep only the first N lines of embedded context files
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt

cat <<'PROMPT_END'
You are in PLAN MODE. Your goal is to produce a detailed, decision-complete
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

PROMPT_END

# Embed any files passed as arguments
for file_path in "${ARGS[@]}"; do
    resolved="$(resolve_input_file "$file_path" 2>/dev/null || true)"
    if [[ -n "${resolved:-}" && -f "$resolved" ]]; then
        embed_file "$resolved"
    elif [[ -f "$file_path" ]]; then
        embed_file "$file_path"
    else
        echo "Warning: File not found: $file_path" >&2
    fi
done
