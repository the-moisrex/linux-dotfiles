#!/usr/bin/env bash
set -euo pipefail

head_lines=""

show_help() {
  cat <<'EOF'
Usage: prompt files [--head N] [FILE...]
       some-command | prompt files [--head N] [FILE...]

Appends the given files as Markdown code blocks.
If you're inside a Git repository, file headings are printed relative to the
repository root.
If no files are provided, `fzf -m` is used to choose them interactively.

Options:
  --head N   Keep only the first N lines of each embedded file
EOF
}


source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"


relative_path() {
  local file="$1"
  if [[ -n "${GIT_ROOT:-}" ]]; then
    realpath --relative-to="$GIT_ROOT" "$file"
  else
    realpath --relative-to="$PWD" "$file"
  fi
}

resolve_input_file() {
  local file="$1"

  if [[ -f "$file" ]]; then
    printf '%s\n' "$file"
    return 0
  fi

  if [[ -n "${GIT_ROOT:-}" && -f "$GIT_ROOT/$file" ]]; then
    printf '%s\n' "$GIT_ROOT/$file"
    return 0
  fi

  return 1
}

select_files() {
  local selected=""

  if ! command -v fzf >/dev/null 2>&1; then
    printf 'prompt files: fzf is required when no files are specified\n' >&2
    exit 1
  fi

  if [[ -n "${GIT_ROOT:-}" ]]; then
    selected="$(
      cd "$GIT_ROOT" &&
      git ls-files --cached --others --exclude-standard | fzf -m
    )"
  else
    selected="$(rg --files | fzf -m)"
  fi

  if [[ -z "$selected" ]]; then
    exit 0
  fi

  while IFS= read -r file; do
    [[ -n "$file" ]] && printf '%s\0' "$file"
  done <<< "$selected"
}


if git rev-parse --show-toplevel >/dev/null 2>&1; then
  GIT_ROOT="$(git rev-parse --show-toplevel)"
else
  GIT_ROOT=""
fi


# printf 'Additional file context:\n\n'

for file in "$@"; do
  if ! resolved_file="$(resolve_input_file "$file")"; then
    printf 'prompt files: file not found: %s\n' "$file" >&2
    continue
  fi

  if [[ ! -r "$resolved_file" ]]; then
    printf 'prompt files: file not readable: %s\n' "$file" >&2
    continue
  fi

  rel_file="$(relative_path "$resolved_file")"
  lang="$(infer_lang "$resolved_file")"

  printf 'File %s\n\n' "$rel_file"
  printf '```%s\n' "$lang"
  trim_context "$(cat -- "$resolved_file")"
  printf '\n```\n\n'
done
