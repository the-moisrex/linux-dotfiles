#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt ask-for-help [--head N]
       echo "I'm stuck on this problem..." | prompt ask-for-help [--head N]

Generates a ready-to-run `prompt` command line that bundles all the context
needed to ask another AI for help with a coding problem.

Describe your problem on stdin (or via clipboard). The AI will search the
repository for relevant files, embed them, and produce a single
`prompt <name> <files...>` command you can hand to another AI session.

Options:
  --head N   Keep only the first N lines of each embedded context file
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

script_dir="$(cd "$(dirname "$0")" && pwd)"

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
cat <<'PROMPTS'
  auto             - Auto-detect the best prompt from input
  cpp              - C++ compiler error analysis and fixes
  cpp-reviewer     - C++ code review
  clang-tidy       - Clang-tidy analysis and fixes
  cmake            - CMake build system help
  ci               - CI/CD pipeline analysis
  cli              - CLI tool design and implementation
  comments         - Add or improve code comments
  commit           - Generate git commit messages
  debug            - Debug crashes, traces, and errors
  de-export        - Remove or fix export/visibility macros
  docker           - Dockerfile and container help
  explain          - Explain how code works
  files            - Embed file contents as context
  fix              - Fix code issues (general)
  gh.issue         - Work on a GitHub issue
  git              - Git-related tasks
  git-dirty        - Show uncommitted changes context
  gtest            - Google Test setup and patterns
  gtest-case       - Find and embed Google Test case source
  migrate          - Code migration between versions/frameworks
  optimize-prompt  - Improve an existing prompt
  plan             - Plan a implementation approach
  refactor         - Refactor code for better structure
  review           - General code review
  run              - Build and run code via CMake targets
  security         - Security analysis and hardening
  spp              - C++ symbol source extraction (parallel)
  symbols          - C++ symbol expansion
  tests            - Generate test cases
  verify           - Verify code correctness
PROMPTS

echo
echo "# Repository file listing (git-tracked)"
echo
git ls-files 2>/dev/null || echo "(not inside a git repo)"
echo
echo "---"
echo

echo "# How the prompt system works"
echo
cat <<'EOF'
The `prompt` dispatcher runs bash scripts under prompts/ that generate AI prompts.
Each script accepts files as arguments and/or stdin. The AI receives the output
of these scripts as context.

Examples of generated commands:
  prompt fix src/main.cpp
  prompt tests parser.h parser.cpp
  prompt debug 2>&1 | prompt auto
  prompt explain docker-compose.yml
EOF

echo
echo "# Your task"
echo
cat <<'EOF'
You are a prompt engineer for a personal dotfiles repository.

Given the problem description above and the repository file listing:

1. Search the file listing for files relevant to the problem.
   - Match by filename, path, extension, and directory structure.
   - If the problem mentions a class/function/error, look for files that likely contain it.
   - Include header files, source files, config files, and build files that are related.
   - When in doubt, include more files — more context is better.

2. Pick the most appropriate prompt script from the list above.
   - Use `auto` if no single prompt fits or if the problem is vague.
   - Use `fix` for bugs/errors, `review` for code quality, `tests` for test generation,
     `debug` for crashes/traces, `explain` for understanding code, `refactor` for restructuring.

3. Output a single, copy-pasteable `prompt` command line in a bash code block:
   ```
   prompt <script-name> <file1> <file2> ...
   ```

4. If the user's problem includes terminal output (compiler errors, stack traces, etc.),
   tell them to pipe it: `command 2>&1 | prompt <script> <files>`

5. If the problem is ambiguous, list 1-2 clarifying questions AFTER the command.

Return:
1. The exact `prompt` command line (bash code block).
2. A one-sentence explanation of why you chose that script and those files.
3. (Optional) Clarifying questions if needed.
EOF
