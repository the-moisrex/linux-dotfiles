#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt ask-for-help [--head N]
       echo "I'm stuck on this problem..." | prompt ask-for-help [--head N]

Generates a ready-to-run `prompt` command line that bundles all the context
needed to ask another AI for help with a coding problem.

Describe your problem on stdin (or via clipboard). The AI will search the
repository for relevant files and produce a single `prompt <name> <files...>`
command you can run, take the output to a smarter AI, and bring back the answer.

Options:
  --head N   Keep only the first N lines of each embedded context file
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

read_stdin || true

if [[ -n "${stdin_content:-}" ]]; then
    printf '%s\n\n' "$stdin_content"
fi

echo "# Problem description"
echo
if [[ -n "${stdin_content:-}" ]]; then
    printf '%s\n' "$stdin_content"
else
    echo "(no description provided — read the clipboard or prompt the user)"
fi
echo
echo "---"
echo

echo "# Available coding-related prompt scripts"
echo
prompt list 2>/dev/null | grep -v -E \
    -e '^(ask-for-help|auto|plan|english|farsi|yt|tweets|note|summarize|man|cppman|metadata|optimize-prompt|skill|new|repo|files|paths)\b' \
    || true

echo
echo "---"
echo

echo "# How this works"
echo
cat <<'EOF'
You are a dumb agentic AI. Your ONLY job is to generate a `prompt` command line.
Do NOT try to solve the problem yourself. Do NOT research anything. Do NOT explain
how things work. Just generate the command.

The workflow is:
1. You generate the `prompt` command below.
2. The user runs it in their terminal.
3. The user takes the command's output and gives it to a smarter chatbot AI.
4. The smart AI's response is brought back to you as context to continue with.

So your output is NOT the solution — it is the COMMAND that produces the context
the smart AI needs.
EOF

echo
echo "# Your task"
echo
cat <<'EOF'
Given the problem description above:

1. Pick the best prompt script from the list above.
   - `fix` for bugs/errors
   - `review` for code quality
   - `tests` for test generation
   - `debug` for crashes/traces
   - `explain` for understanding code
   - `refactor` for restructuring
   - `clang-tidy` for lint issues
   - `cpp` / `cpp-reviewer` for C++ specific
   - `docker` / `ci` for container/CI configs
   - `commit` for git commit messages
   - `gh.issue` to work on a GitHub issue
   - `security` for security review
   - `verify` to check correctness of changes

2. Output ONE command in a bash code block:
   ```
   prompt <script-name> <file1> <file2> ...
   ```
   For problems with terminal output (compiler errors, stack traces, etc.),
   tell the user to pipe it:
   ```
   command 2>&1 | prompt <script> <files>
   ```

3. If the problem is ambiguous, list 1-2 clarifying questions AFTER the command.

Do NOT:
- Explain what the command does
- Research how to solve the problem
- Provide any solution or analysis
- Do anything beyond generating the command
EOF
