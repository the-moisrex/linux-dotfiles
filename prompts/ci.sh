#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt ci [--head N] [FILE...]
       echo "task description" | prompt ci [--head N] [FILE...]

CI/CD pipeline review prompt.
Analyzes GitHub Actions, GitLab CI, Jenkins, or other CI/CD configurations.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "You are a CI/CD pipeline expert."
echo "Review the provided pipeline configurations for correctness, security, and efficiency."
echo
echo "Focus on:"
echo "- Security: secrets handling, GITHUB_TOKEN scope, artifact permissions"
echo "- Reliability: flaky test handling, retry logic, timeout configuration"
echo "- Speed: parallel jobs, caching strategies, dependency optimization"
echo "- Maintainability: reusable workflows, matrix builds, clear job naming"
echo "- Best practices: pinned action versions, branch protection, status checks"
echo "- Cost: unused jobs, excessive runner minutes, artifact retention"
echo
echo "Provide specific improvements as a git diff."
echo

for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
        echo
    else
        echo "Warning: File '$file' not found." >&2
    fi
done
