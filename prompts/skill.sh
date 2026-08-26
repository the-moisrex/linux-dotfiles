#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt skill <name> [--head N] [FILE...]
       some-command | prompt skill <name> [--head N] [FILE...]
       prompt skill list [--remote <query>]
       prompt skill install <name>
       prompt skill uninstall <name>

Loads a cached skill and outputs its content as an AI prompt.
If the skill is not cached, it is downloaded automatically.
Piped stdin and file arguments are embedded as context.

Subcommands:
  list              List locally cached skills
  list --remote Q   Search remote registries for skills matching Q
  install <name>    Download a specific skill by exact name
  uninstall <name>  Remove a cached skill

Options:
  --head N   Keep only the first N lines of the embedded context
  -h, --help Show this help message

Examples:
  prompt skill docker                      # load cached docker skill
  cat file.cpp | prompt skill cpp-testing  # load skill + embed file
  prompt skill list                        # show cached skills
  prompt skill install docker              # download docker skill
EOF
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AI_FIND_SKILLS="$SCRIPT_DIR/../bin/ai-find-skills"
SKILLS_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/agents/skills"
LEGACY_DIRS=("$HOME/.agents/skills" "$HOME/.claude/skills")

command -v jq >/dev/null 2>&1 || { echo "error: jq is required" >&2; exit 1; }

# --- helpers ---------------------------------------------------------------

parse_frontmatter() {
  local file="$1"
  local in_fm=false fm_lines=""
  while IFS= read -r line; do
    if [[ "$line" == "---" ]]; then
      if $in_fm; then break
      else in_fm=true; continue
      fi
    fi
    $in_fm && fm_lines+="$line"$'\n'
  done < "$file"
  echo "$fm_lines" | python3 -c "
import sys, json, re
data = {}
for line in sys.stdin:
    line = line.rstrip()
    m = re.match(r'^(\w[\w_-]*):\s*(.*)', line)
    if m:
        k, v = m.group(1), m.group(2).strip()
        if len(v) >= 2 and v[0] == v[-1] and v[0] in ('\"', \"'\"):
            v = v[1:-1]
        data[k] = v
print(json.dumps(data))
" 2>/dev/null || echo '{}'
}

list_cached_skills() {
  local found=0
  for dir in "$SKILLS_DIR" "${LEGACY_DIRS[@]}"; do
    [[ -d "$dir" ]] || continue
    for skill_dir in "$dir"/*/; do
      [[ -d "$skill_dir" ]] || continue
      local skill_file="$skill_dir/SKILL.md"
      [[ -f "$skill_file" ]] || continue
      local meta
      meta="$(parse_frontmatter "$skill_file")"
      local name desc
      name="$(jq -r '.name // empty' <<<"$meta")"
      [[ -z "$name" ]] && name="$(basename "$skill_dir")"
      desc="$(jq -r '.description // ""' <<<"$meta")"
      if [[ ${#desc} -gt 80 ]]; then
        desc="${desc:0:77}..."
      fi
      printf '  %-24s %s\n' "$name" "$desc"
      found=1
    done
  done
  if [[ $found -eq 0 ]]; then
    echo "  (no cached skills found)"
    echo
    echo "  Skills are cached in: $SKILLS_DIR"
    echo "  Use 'prompt skill <query>' to search and download skills."
  fi
}

search_skills() {
  local query="$1"
  if [[ ! -x "$AI_FIND_SKILLS" ]]; then
    echo "error: ai-find-skills not found at $AI_FIND_SKILLS" >&2
    exit 1
  fi
  "$AI_FIND_SKILLS" --json --no-scan "$query"
}

merge_results() {
  jq -c '
    [
      (.skills_sh // [])[] | {name, board, repo, slug, url, installs, stars, match, summary},
      (.clawhub    // [])[] | {name, board, repo:null, slug, url, installs, stars, match, summary},
      (.github     // [])[] | {name, board, repo, slug:null, url, installs:null, stars, match, summary}
    ] | to_entries | map(.key + 1 as $idx | .value | {idx: $idx, name, board, repo, slug, url, installs, stars, match, summary})
  '
}

display_results() {
  local results="$1"
  if [[ "$(jq 'length' <<<"$results")" -eq 0 ]]; then
    echo "  No skills found."
    return 1
  fi
  jq -r '.[] |
    "  [\(.idx)] \(.name)  (\(.board))" +
    "  match \(.match)%" +
    (if .installs != null then "  · \(.installs) installs" else "" end) +
    (if .stars != null and .stars > 0 then "  · ★ \(.stars)" else "" end) +
    "\n" +
    (if .summary then "      \(.summary[0:100])\n" else "" end) +
    "      \(.url)"
  ' <<<"$results"
}

download_skill_md() {
  local rec="$1"
  local name repo slug url board tmp_body
  name="$(jq -r '.name' <<<"$rec")"
  repo="$(jq -r '.repo // empty' <<<"$rec")"
  slug="$(jq -r '.slug // empty' <<<"$rec")"
  url="$(jq -r '.url' <<<"$rec")"
  board="$(jq -r '.board' <<<"$rec")"
  tmp_body="$(mktemp)"

  if [[ -n "$repo" ]] && command -v gh >/dev/null 2>&1; then
    local br path
    br="$(gh api "repos/$repo" --jq '.default_branch' 2>/dev/null)" || true
    if [[ -n "$br" ]]; then
      path="$(gh api "repos/$repo/git/trees/$br?recursive=1" --jq \
        "[.tree[].path | select(endswith(\"SKILL.md\")) | select(test(\"(^|/)$name/SKILL.md\$\";\"i\"))][0]" 2>/dev/null)" || true
      [[ -z "$path" ]] && path="$(gh api "repos/$repo/git/trees/$br?recursive=1" --jq \
        '[.tree[].path | select(endswith("SKILL.md"))][0]' 2>/dev/null)" || true
      if [[ -n "$path" ]]; then
        curl -sS --max-time 15 "https://raw.githubusercontent.com/$repo/$br/$path" -o "$tmp_body" 2>/dev/null
        if [[ -s "$tmp_body" ]]; then
          echo "$tmp_body"
          return 0
        fi
      fi
    fi
  fi

  if [[ -n "$slug" ]] && command -v unzip >/dev/null 2>&1; then
    local tmp_zip
    tmp_zip="$(mktemp)"
    curl -sS --max-time 15 "https://clawhub.ai/api/download?slug=$slug" -o "$tmp_zip" 2>/dev/null
    if [[ -s "$tmp_zip" ]]; then
      unzip -p "$tmp_zip" SKILL.md > "$tmp_body" 2>/dev/null
      if [[ -s "$tmp_body" ]]; then
        rm -f "$tmp_zip"
        echo "$tmp_body"
        return 0
      fi
      unzip -l "$tmp_zip" 2>/dev/null | grep -i 'SKILL.md' | head -1 | awk '{print $NF}' | while read -r entry; do
        unzip -p "$tmp_zip" "$entry" > "$tmp_body" 2>/dev/null
      done
      if [[ -s "$tmp_body" ]]; then
        rm -f "$tmp_zip"
        echo "$tmp_body"
        return 0
      fi
      rm -f "$tmp_zip"
    fi
  fi

  if [[ "$url" == skills.sh/* ]] || [[ "$url" == *skills.sh/* ]]; then
    local ss_owner ss_repo ss_rest
    ss_rest="${url#*skills.sh/}"
    ss_owner="$(echo "$ss_rest" | cut -d/ -f1)"
    ss_repo="$(echo "$ss_rest" | cut -d/ -f2)"
    if [[ -n "$ss_owner" && -n "$ss_repo" ]]; then
      local ss_github="$ss_owner/$ss_repo"
      local ss_br ss_path
      ss_br="$(gh api "repos/$ss_github" --jq '.default_branch' 2>/dev/null)" || true
      if [[ -n "$ss_br" ]]; then
        ss_path="$(gh api "repos/$ss_github/git/trees/$ss_br?recursive=1" --jq \
          "[.tree[].path | select(endswith(\"SKILL.md\")) | select(test(\"(^|/)$name/SKILL.md\$\";\"i\"))][0]" 2>/dev/null)" || true
        [[ -z "$ss_path" ]] && ss_path="$(gh api "repos/$ss_github/git/trees/$ss_br?recursive=1" --jq \
          '[.tree[].path | select(endswith("SKILL.md"))][0]' 2>/dev/null)" || true
        if [[ -n "$ss_path" ]]; then
          curl -sS --max-time 15 "https://raw.githubusercontent.com/$ss_github/$ss_br/$ss_path" -o "$tmp_body" 2>/dev/null
          if [[ -s "$tmp_body" ]]; then
            echo "$tmp_body"
            return 0
          fi
        fi
      fi
    fi
  fi

  if [[ "$url" == *raw.githubusercontent.com* ]]; then
    curl -sS --max-time 15 "$url" -o "$tmp_body" 2>/dev/null
    if [[ -s "$tmp_body" ]]; then
      echo "$tmp_body"
      return 0
    fi
  fi

  rm -f "$tmp_body"
  return 1
}

find_cached_skill() {
  local name="$1"
  for dir in "$SKILLS_DIR" "${LEGACY_DIRS[@]}"; do
    if [[ -f "$dir/$name/SKILL.md" ]]; then
      echo "$dir/$name/SKILL.md"
      return 0
    fi
  done
  return 1
}

download_by_name() {
  local name="$1"
  local results rec
  results="$(search_skills "$name" 2>/dev/null | merge_results)"
  rec="$(jq -c --arg n "$name" '[.[] | select(.name == $n)] | .[0] // empty' <<<"$results")"
  [[ -z "$rec" ]] && rec="$(jq -c --arg n "$name" '[.[] | select(.name | test($n; "i"))] | .[0] // empty' <<<"$results")"
  [[ -z "$rec" ]] && return 1

  local skill_name tmp_body
  skill_name="$(jq -r '.name' <<<"$rec")"
  mkdir -p "$SKILLS_DIR/$skill_name"
  tmp_body="$(download_skill_md "$rec")"
  if [[ -z "$tmp_body" ]] || [[ ! -s "$tmp_body" ]]; then
    rm -rf "$SKILLS_DIR/$skill_name"
    return 1
  fi
  cp "$tmp_body" "$SKILLS_DIR/$skill_name/SKILL.md"
  rm -f "$tmp_body"
  echo "$SKILLS_DIR/$skill_name/SKILL.md"
}

install_skill() {
  local name="$1"
  local rec="${2:-}"

  if [[ -z "$rec" ]]; then
    echo "Searching for '$name'..." >&2
    local results
    results="$(search_skills "$name" 2>/dev/null | merge_results)"
    rec="$(jq -c --arg n "$name" '[.[] | select(.name == $n)] | .[0] // empty' <<<"$results")"
    if [[ -z "$rec" ]]; then
      rec="$(jq -c --arg n "$name" '[.[] | select(.name | test($n; "i"))] | .[0] // empty' <<<"$results")"
    fi
    if [[ -z "$rec" ]]; then
      echo "error: skill '$name' not found in any registry" >&2
      exit 1
    fi
    echo "Found: $(jq -r '.name' <<<"$rec") on $(jq -r '.board' <<<"$rec")" >&2
  fi

  local skill_name
  skill_name="$(jq -r '.name' <<<"$rec")"
  mkdir -p "$SKILLS_DIR/$skill_name"

  echo "Downloading $skill_name..." >&2
  local tmp_body
  tmp_body="$(download_skill_md "$rec")"
  if [[ -z "$tmp_body" ]] || [[ ! -s "$tmp_body" ]]; then
    echo "error: failed to download SKILL.md for '$skill_name'" >&2
    rm -rf "$SKILLS_DIR/$skill_name"
    exit 1
  fi

  cp "$tmp_body" "$SKILLS_DIR/$skill_name/SKILL.md"
  rm -f "$tmp_body"

  echo "Installed '$skill_name' to $SKILLS_DIR/$skill_name/SKILL.md" >&2
  echo
  echo "Installed skill content:"
  echo
  cat "$SKILLS_DIR/$skill_name/SKILL.md"
}

uninstall_skill() {
  local name="$1"
  local found=false
  for dir in "$SKILLS_DIR" "${LEGACY_DIRS[@]}"; do
    if [[ -d "$dir/$name" ]]; then
      rm -rf "$dir/$name"
      echo "Uninstalled '$name' from $dir/$name"
      found=true
    fi
  done
  if ! $found; then
    echo "error: skill '$name' not found in any cache directory" >&2
    exit 1
  fi
}

# --- main ------------------------------------------------------------------

source "$SCRIPT_DIR/_common.sh"

case "${1:-}" in
  list|install|uninstall)
    # Subcommands don't need stdin or file context
    NO_FILES=true
    common_behavior
    set -- "${ARGS[@]}"

    SUBCOMMAND="$1"; shift
    case "$SUBCOMMAND" in
      list)
        REMOTE_QUERY=""
        if [[ "${1:-}" == "--remote" ]]; then
          REMOTE_QUERY="${2:-}"
          shift 2
        fi
        if [[ -n "$REMOTE_QUERY" ]]; then
          echo "Searching remote registries for '$REMOTE_QUERY'..." >&2
          json="$(search_skills "$REMOTE_QUERY")"
          results="$(merge_results <<<"$json")"
          echo
          echo "Remote skills matching '$REMOTE_QUERY':"
          echo
          display_results "$results"
        else
          echo "Cached skills:"
          echo
          list_cached_skills
        fi
        ;;

      install)
        if [[ $# -eq 0 ]]; then
          echo "error: 'prompt skill install' requires a skill name" >&2
          echo "Usage: prompt skill install <name>" >&2
          exit 2
        fi
        install_skill "$1"
        ;;

      uninstall)
        if [[ $# -eq 0 ]]; then
          echo "error: 'prompt skill uninstall' requires a skill name" >&2
          echo "Usage: prompt skill uninstall <name>" >&2
          exit 2
        fi
        uninstall_skill "$1"
        ;;
    esac
    exit 0
    ;;
esac

# Default mode: prompt skill <name> [FILE...]
# Skill content is the instruction — output it first, then context.
parse_arguments
set -- "${ARGS[@]}"

SKILL_NAME="${1:-}"
shift 2>/dev/null || true

if [[ -z "$SKILL_NAME" ]]; then
  echo "error: skill name required" >&2
  echo "Usage: prompt skill <name> [FILE...]" >&2
  echo "       prompt skill list   # to see cached skills" >&2
  exit 2
fi

SKILL_FILE="$(find_cached_skill "$SKILL_NAME" 2>/dev/null)" || true
if [[ -z "$SKILL_FILE" ]]; then
  echo "Skill '$SKILL_NAME' not cached, downloading..." >&2
  SKILL_FILE="$(download_by_name "$SKILL_NAME" 2>/dev/null)" || true
  if [[ -z "$SKILL_FILE" ]]; then
    echo "error: skill '$SKILL_NAME' not found in cache or registries" >&2
    echo "Use 'prompt skill list --remote $SKILL_NAME' to search registries." >&2
    exit 1
  fi
  echo "Downloaded to $SKILL_FILE" >&2
fi

# Output skill content first (the AI instruction)
cat -- "$SKILL_FILE"

# Now embed piped stdin if present
if ! [ -t 0 ]; then
  stdin_content="$(cat)"
  if [[ -n "$stdin_content" ]]; then
    echo
    printf '%s\n' "$stdin_content"
  fi
fi

# Embed file arguments
for file in "$@"; do
  if [[ -f "$file" ]]; then
    file_name="$(basename "$file")"
    echo
    echo "File: $file_name"
    echo
    echo "\`\`\`$(infer_lang "$file_name")"
    trim_context "$(cat -- "$file")"
    echo '```'
  else
    echo "Warning: File '$file' not found." >&2
  fi
done
