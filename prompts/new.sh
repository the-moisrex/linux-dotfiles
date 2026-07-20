#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt new [--head N] [FILE...]
       echo "what the prompt should do" | prompt new [--head N] [FILE...]

Builds a prompt that asks an AI to write a new bash prompt script
following this repository's prompt conventions.

Always embeds shared infrastructure and example prompt scripts so the AI
has enough context to generate a good, consistent prompt script.

Provide the desired prompt behavior on stdin and/or extra reference
files as arguments.

Options:
  --head N   Keep only the first N lines of each embedded context file
EOF
}

# Prevent _common.sh from triggering fzf when no files are passed
NO_FILES=true

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

script_dir="$(cd "$(dirname "$0")" && pwd)"

# Manually handle stdin since we disabled print_stdin in _common.sh
if ! [ -t 0 ]; then
    cat
    echo
fi

echo "Write a new bash prompt script for this repository."
echo
echo "Goal:"
echo "Create a complete, ready-to-use script under prompts/ that generates an AI prompt."
echo "Match the style, structure, and helpers used by the existing prompt scripts."
echo
echo "Requirements for the generated script:"
echo "- Start with a proper bash shebang and 'set -euo pipefail' unless there is a strong reason not to."
echo "- Define a show_help function and support --help/-h via the shared argument parser."
echo "- The script will be called using a wrapper script named 'prompt' (e.g., 'prompt my-script'). Ensure the help menu usage reflects this (e.g., 'Usage: prompt my-script')."
echo "- Source prompts/_common.sh and call common_behavior, then 'set -- \"\${ARGS[@]}\"'."
echo "- Support optional file arguments and stdin the same way the other prompt scripts do."
echo "- Use infer_lang and trim_context when embedding file contents."
echo "- Print clear AI instructions first, then embed any needed context as fenced code blocks."
echo "- Prefer actionable output from the AI (for example tables, checklists, or a git diff) when that fits the task."
echo "- Keep the script focused, minimal, and consistent with repository conventions."
echo "- Do not invent APIs that are not present in _common.sh unless they are pure local helpers."
echo
echo "If a task description appears above or below, implement exactly that prompt behavior."
echo "If extra reference files are provided, treat them as examples or domain context for the new prompt."
echo
echo "Return:"
echo "1. A short explanation of the design choices."
echo "2. The full bash script contents."
echo "3. A suggested filename under prompts/."
echo "4. One or two example invocations."

# Core context the AI needs to generate a good prompt script
embed_file "$script_dir/_common.sh" "prompts/_common.sh"
embed_file "$script_dir/fix.sh" "prompts/fix.sh (example)"
embed_file "$script_dir/symbols.sh" "prompts/symbols.sh (example)"

# Optional extra context from the caller
for file_path in "$@"; do
    resolved="$(resolve_input_file "$file_path" 2>/dev/null || true)"
    if [[ -n "${resolved:-}" && -f "$resolved" ]]; then
        embed_file "$resolved"
        elif [[ -f "$file_path" ]]; then
        embed_file "$file_path"
    else
        echo "Warning: File not found or is not a regular file: $file_path" >&2
    fi
done

