#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt gh.issue <ISSUE_NUMBER>
       echo "issue context" | prompt gh.issue [ISSUE_NUMBER]

Fetch a GitHub issue and generate a prompt asking the AI to implement it.
Uses `gh issue view` to retrieve issue details. If `gh` is unavailable,
reads issue context from stdin or produces a generic implementation prompt.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
NO_FILES=true

# Capture stdin before common_behavior consumes it
STDIN_CONTEXT=""
if ! [ -t 0 ]; then
    STDIN_CONTEXT="$(cat)"
fi

common_behavior
set -- "${ARGS[@]}"

ISSUE_NUMBER="${1:-}"
ISSUE_TITLE=""
ISSUE_BODY=""
ISSUE_LABELS=""
ISSUE_URL=""
ISSUE_COMMENTS=""

if [[ -n "$ISSUE_NUMBER" ]]; then
    if command -v gh >/dev/null 2>&1; then
        ISSUE_URL="$(gh issue view "$ISSUE_NUMBER" --json url -q .url 2>/dev/null || true)"
        ISSUE_TITLE="$(gh issue view "$ISSUE_NUMBER" --json title -q .title 2>/dev/null || true)"
        ISSUE_BODY="$(gh issue view "$ISSUE_NUMBER" --json body -q .body 2>/dev/null || true)"
        ISSUE_LABELS="$(gh issue view "$ISSUE_NUMBER" --json labels -q '[.[].name]' 2>/dev/null || true)"
        ISSUE_COMMENTS="$(gh issue view "$ISSUE_NUMBER" --json comments -q '.comments | map("**\(.author.login)**: \(.body)") | join("\n\n")' 2>/dev/null || true)"
    else
        echo "prompt gh.issue: Warning: 'gh' CLI is not installed. Falling back to stdin." >&2
        if [[ -z "$STDIN_CONTEXT" ]]; then
            echo "prompt gh.issue: No issue data available. Pipe issue details or install gh CLI." >&2
            exit 1
        fi
    fi
fi

cat <<'PROMPT_END'
Implement the following GitHub issue. Read the issue carefully, explore the
codebase to understand the current state, and produce a working implementation.
Follow existing code conventions, patterns, and project structure.

## Requirements

- Read relevant source files before writing any code.
- Make minimal, focused changes that solve the problem.
- Preserve existing code style and conventions.
- Do not add unnecessary abstractions or rewrites.
- Return your changes as a git diff that can be applied directly.

PROMPT_END

echo "---"
echo

if [[ -n "$ISSUE_TITLE" ]]; then
    echo "# Issue #$ISSUE_NUMBER: $ISSUE_TITLE"
    echo
    if [[ -n "$ISSUE_URL" ]]; then
        echo "URL: $ISSUE_URL"
    fi
    if [[ -n "$ISSUE_LABELS" ]]; then
        echo "Labels: $ISSUE_LABELS"
    fi
    echo
    if [[ -n "$ISSUE_BODY" ]]; then
        echo "$ISSUE_BODY"
    fi
    if [[ -n "$ISSUE_COMMENTS" ]]; then
        echo
        echo "## Comments"
        echo
        echo "$ISSUE_COMMENTS"
    fi
elif [[ -n "$STDIN_CONTEXT" ]]; then
    if [[ -n "$ISSUE_NUMBER" ]]; then
        echo "# Issue #$ISSUE_NUMBER (provided via stdin)"
    else
        echo "# Issue Description (provided via stdin)"
    fi
    echo
    echo "$STDIN_CONTEXT"
else
    echo "No issue data available." >&2
    exit 1
fi

echo
