#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt ci [--head N]
       echo "task description" | prompt ci [--head N]

CI/CD pipeline review prompt.
Automatically finds and embeds all CI/CD configuration files from the repository
(GitHub Actions, GitLab CI, Jenkins, Docker, etc.).

Options:
  --head N   Keep only the first N lines of each embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

script_dir="$(cd "$(dirname "$0")" && pwd)"
find_git_root

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

# Find CI/CD config files from the git repository
_ci_files=()

if [[ -n "${GIT_ROOT:-}" ]]; then
    # GitHub Actions workflows
    while IFS= read -r f; do
        [[ -n "$f" ]] && _ci_files+=("$GIT_ROOT/$f")
    done < <(git -C "$GIT_ROOT" ls-files -- '*.yml' '*.yaml' 2>/dev/null | grep -iE '\.github/workflows/')

    # GitLab CI
    while IFS= read -r f; do
        [[ -n "$f" ]] && _ci_files+=("$GIT_ROOT/$f")
    done < <(git -C "$GIT_ROOT" ls-files -- '.gitlab-ci.yml' '.gitlab-ci.yaml' '**/.gitlab-ci.yml' '**/.gitlab-ci.yaml' 2>/dev/null)

    # Jenkinsfiles
    while IFS= read -r f; do
        [[ -n "$f" ]] && _ci_files+=("$GIT_ROOT/$f")
    done < <(git -C "$GIT_ROOT" ls-files -- '*Jenkinsfile*' 2>/dev/null)

    # Dockerfiles and docker-compose
    while IFS= read -r f; do
        [[ -n "$f" ]] && _ci_files+=("$GIT_ROOT/$f")
    done < <(git -C "$GIT_ROOT" ls-files -- '*Dockerfile*' '*dockerfile*' '*docker-compose*' '*compose.y*' 2>/dev/null)

    # CircleCI
    while IFS= read -r f; do
        [[ -n "$f" ]] && _ci_files+=("$GIT_ROOT/$f")
    done < <(git -C "$GIT_ROOT" ls-files -- '.circleci/config.yml' '**/.circleci/config.yml' 2>/dev/null)

    # Travis CI
    while IFS= read -r f; do
        [[ -n "$f" ]] && _ci_files+=("$GIT_ROOT/$f")
    done < <(git -C "$GIT_ROOT" ls-files -- '.travis.yml' '**/.travis.yml' 2>/dev/null)
fi

if [[ ${#_ci_files[@]} -eq 0 ]]; then
    echo "No CI/CD configuration files found in this repository."
    echo "Searched for: GitHub Actions, GitLab CI, Jenkins, Docker, CircleCI, Travis CI."
    exit 0
fi

# Deduplicate
declare -A _seen=()
for file in "${_ci_files[@]}"; do
    [[ -z "${_seen[$file]:-}" ]] || continue
    _seen[$file]=1

    if [[ -f "$file" ]]; then
        label="$(realpath --relative-to="${GIT_ROOT:-$PWD}" "$file" 2>/dev/null || basename "$file")"
        echo "File: $label"
        echo
        echo "\`\`\`$(infer_lang "$file")"
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
        echo
    fi
done
