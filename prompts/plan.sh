#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt plan [--head N] [FILE...]
       echo "task description" | prompt plan [--head N] [FILE...]

Generates a structured plan mode prompt for an AI assistant.
The AI will be instructed to explore the environment, ask clarifying questions,
and produce a detailed implementation plan before writing any code.

Use this when you want the AI to plan before implementing.

Options:
  --head N   Keep only the first N lines of embedded context files
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

cat <<'PROMPT_END'
You are in PLAN MODE. Your goal is to produce a detailed, decision-complete
implementation plan before writing any code.

## Process

1. **Explore the environment** - Read relevant files, configs, schemas, types,
   and existing code to understand the current state.
2. **Ask questions** - If anything is unclear, ambiguous, or missing, ask
   specific questions. Tell the user what files or context you need.
3. **Identify constraints** - Note any relevant constraints, assumptions,
   edge cases, or dependencies.
4. **Produce a structured plan** - Only after gathering enough context.

## Plan Structure

Your plan must include these sections:

- **Goal** - What we're building and why
- **Success criteria** - How we'll know it's done
- **Approach** - High-level design decisions and tradeoffs
- **Implementation steps** - Concrete, ordered steps
- **Files to change** - Specific files and what changes they need
- **Test plan** - How to verify correctness
- **Assumptions** - What you're assuming (and what needs confirmation)

## Rules

- Do NOT write any code or make changes yet.
- Do NOT skip exploration - read the relevant files first.
- If you need more context (files, configs, specs), ask clearly.
- Ask the user for any clarifications needed to make the plan
  decision-complete.
- Be specific about files and line numbers when referencing existing code.

PROMPT_END

# Embed any files passed as arguments
for file_path in "$@"; do
    resolved="$(resolve_input_file "$file_path" 2>/dev/null || true)"
    if [[ -n "${resolved:-}" && -f "$resolved" ]]; then
        embed_file "$resolved"
    elif [[ -f "$file_path" ]]; then
        embed_file "$file_path"
    else
        echo "Warning: File not found: $file_path" >&2
    fi
done
