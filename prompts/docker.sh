#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt docker [--head N] [FILE...]
       echo "task description" | prompt docker [--head N] [FILE...]

Docker and container review prompt.
Analyzes Dockerfiles, docker-compose files, and container configurations for best practices.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "You are a Docker and container infrastructure expert."
echo "Review the provided Dockerfiles, docker-compose files, or container configurations."
echo
echo "Focus on:"
echo "- Security: non-root users, minimal base images, secrets exposure, COPY vs ADD"
echo "- Layer optimization: ordering, caching, multi-stage builds, .dockerignore"
echo "- Image size: unnecessary packages, build artifacts, dev dependencies in production"
echo "- docker-compose: service dependencies, health checks, resource limits, networking"
echo "- Runtime: signal handling, logging, volume mounts, environment variable management"
echo "- Best practices: LABEL metadata, ENTRYPOINT vs CMD, ARG vs ENV"
echo
echo "Provide specific fixes as a git diff. Prioritize security and size improvements."
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
