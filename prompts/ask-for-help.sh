#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt ask-for-help [--head N]
       echo "I'm stuck on this problem..." | prompt ask-for-help [--head N]

Generates a ready-to-run `prompt` command line that bundles all the context
needed to ask another AI for help with a coding problem.

Describe your problem on stdin (or via clipboard). The AI will compose a
`prompt` command using context-gathering prompts (.note, .cli, .files, etc.)
that you can run, take the output to a smarter AI, and bring back the answer.

Options:
  --head N   Keep only the first N lines of each embedded context file
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

echo "# Available context-gathering prompts"
echo
echo "Use these with the shorthand syntax to compose your command:"
echo "  .note \"text\"        Add a note or context description"
echo "  .cli \"command\"      Run a shell command and embed its output"
echo "  .files file1 file2  Embed file contents as fenced code blocks"
echo "  .agents             Embed agent instruction files (AGENTS.md, etc.)"
echo "  .git-dirty          Show uncommitted changes in the repo"
echo "  .repo               Show the repository file structure"
echo "  .clipboard          Embed clipboard content"
echo
echo "Example command:"
echo "  prompt .note \"WIP: fixing parser bug\" .files parser.h parser.cpp .cli \"make test 2>&1\""
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
Given the problem description above, compose a `prompt` command that gathers
all the context a smarter AI would need to solve the problem.

Use the shorthand syntax:
  prompt .note "problem description" .files <relevant files> .cli "command to reproduce"

Guidelines:
- Always start with .note describing the problem in detail.
- Add .files for any source files, configs, or logs relevant to the problem.
- Add .cli for commands that reproduce the error or show relevant state
  (e.g., .cli "make 2>&1", .cli "git diff", .cli "ls -la src/").
- Use .git-dirty if uncommitted changes are relevant.
- Use .agents if the project has AGENTS.md or similar context files.
- Use .repo if the AI needs to understand the project structure.

Output ONE command in a bash code block:
```
prompt .note "..." .files ... .cli "..." ...
```

If the problem is ambiguous, list 1-2 clarifying questions AFTER the command.

Do NOT:
- Explain what the command does
- Research how to solve the problem
- Provide any solution or analysis
- Do anything beyond generating the command
EOF
