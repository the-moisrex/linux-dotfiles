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

infer_lang() {
  local file="$1"
  local base ext

  base="$(basename "$file")"
  ext="${base##*.}"

  case "$base" in
    Dockerfile) echo "dockerfile" ;;
    Makefile|makefile|GNUmakefile) echo "makefile" ;;
    CMakeLists.txt) echo "cmake" ;;
    *) case "$ext" in
      c) echo "c" ;;
      h) echo "c" ;;
      cc|cp|cpp|cxx|c++|hpp|hxx|hh|h++) echo "cpp" ;;
      m) echo "objectivec" ;;
      mm) echo "objective-cpp" ;;
      rs) echo "rust" ;;
      py|pyi) echo "python" ;;
      sh|bash) echo "bash" ;;
      zsh) echo "zsh" ;;
      fish) echo "fish" ;;
      nu) echo "nu" ;;
      js|cjs|mjs) echo "javascript" ;;
      ts|mts|cts) echo "typescript" ;;
      jsx) echo "jsx" ;;
      tsx) echo "tsx" ;;
      java) echo "java" ;;
      kt|kts) echo "kotlin" ;;
      swift) echo "swift" ;;
      go) echo "go" ;;
      rb) echo "ruby" ;;
      php) echo "php" ;;
      lua) echo "lua" ;;
      pl|pm) echo "perl" ;;
      r) echo "r" ;;
      scala) echo "scala" ;;
      cs) echo "csharp" ;;
      fs|fsx) echo "fsharp" ;;
      vb) echo "vbnet" ;;
      dart) echo "dart" ;;
      ex|exs) echo "elixir" ;;
      erl|hrl) echo "erlang" ;;
      clj|cljs|cljc) echo "clojure" ;;
      ml|mli) echo "ocaml" ;;
      sql) echo "sql" ;;
      html|htm) echo "html" ;;
      css) echo "css" ;;
      scss) echo "scss" ;;
      sass) echo "sass" ;;
      less) echo "less" ;;
      xml) echo "xml" ;;
      xsl|xslt) echo "xslt" ;;
      svg) echo "svg" ;;
      json) echo "json" ;;
      jsonc) echo "jsonc" ;;
      yaml|yml) echo "yaml" ;;
      toml) echo "toml" ;;
      ini|cfg|conf) echo "ini" ;;
      env) echo "dotenv" ;;
      md) echo "markdown" ;;
      txt|log) echo "text" ;;
      diff|patch) echo "diff" ;;
      proto) echo "proto" ;;
      asm|s|S) echo "asm" ;;
      tex) echo "tex" ;;
      vim) echo "vim" ;;
      *) echo "text" ;;
    esac ;;
  esac
}

relative_path() {
  local file="$1"
  if [[ -n "${GIT_ROOT:-}" ]]; then
    realpath --relative-to="$GIT_ROOT" "$file"
  else
    realpath --relative-to="$PWD" "$file"
  fi
}

trim_context() {
  local content="$1"
  if [[ -n "$head_lines" ]]; then
    printf '%s' "$content" | head -n "$head_lines"
  else
    printf '%s' "$content"
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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      show_help
      exit 0
      ;;
    --head)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --head" >&2
        exit 2
      fi
      head_lines="$2"
      shift 2
      ;;
    *)
      break
      ;;
  esac
done

if git rev-parse --show-toplevel >/dev/null 2>&1; then
  GIT_ROOT="$(git rev-parse --show-toplevel)"
else
  GIT_ROOT=""
fi

stdin_content=""
if ! [ -t 0 ]; then
  stdin_content="$(cat)"
fi

if [[ -n "$stdin_content" ]]; then
  printf '%s' "$stdin_content"
fi

if [[ $# -eq 0 ]]; then
  mapfile -d '' -t selected_files < <(select_files)
  set -- "${selected_files[@]}"
fi

if [[ $# -eq 0 ]]; then
  exit 0
fi

if [[ -n "$stdin_content" ]]; then
  printf '\n\n'
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

  printf '### %s\n\n' "$rel_file"
  printf '```%s\n' "$lang"
  trim_context "$(cat -- "$resolved_file")"
  printf '\n```\n\n'
done
