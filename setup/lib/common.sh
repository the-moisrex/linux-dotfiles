#!/usr/bin/env bash

set -u

VERBOSE=false
UNINSTALL=false

if [[ -t 1 ]]; then
  C_RESET=$'\033[0m'
  C_INFO=$'\033[1;34m'
  C_STEP=$'\033[0;36m'
  C_WARN=$'\033[1;33m'
  C_ERROR=$'\033[1;31m'
else
  C_RESET=''
  C_INFO=''
  C_STEP=''
  C_WARN=''
  C_ERROR=''
fi

log() {
  echo "${C_INFO}$*${C_RESET}"
}

log_step() {
  echo "${C_STEP}  $*${C_RESET}"
}

log_verbose() {
  if [[ "$VERBOSE" == "true" ]]; then
    echo "    $*"
  fi
}

warn() {
  echo "${C_WARN}WARNING:${C_RESET} $*" >&2
}

die() {
  echo "${C_ERROR}ERROR:${C_RESET} $*" >&2
  exit 1
}

run_cmd() {
  if [[ "$VERBOSE" == "true" ]]; then
    log_verbose "Running: $*"
    "$@"
    return
  fi

  local output
  if ! output=$("$@" 2>&1); then
    [[ -n "$output" ]] && echo "$output" >&2
    die "Command failed: $*"
  fi
}

run_cmd_may_fail() {
  if [[ "$VERBOSE" == "true" ]]; then
    log_verbose "Running (allowed failure): $*"
    "$@" || return 0
    return
  fi

  local output
  output=$("$@" 2>&1) || {
    [[ -n "$output" ]] && warn "$output"
    warn "Command failed (ignored): $*"
    return 0
  }
}

replace_or_append_kv() {
  local file="$1"
  local key="$2"
  local value="$3"

  if [[ ! -f "$file" ]]; then
    return 0
  fi

  if grep -qE "^\s*${key}\b" "$file"; then
    run_cmd_may_fail sudo sed -i "s|^\s*${key}.*|${key} ${value}|" "$file"
  else
    log_verbose "Appending ${key} ${value} to $file"
    if ! printf '%s %s\n' "$key" "$value" | sudo tee -a "$file" >/dev/null 2>&1; then
      warn "Failed to append ${key} to $file"
    fi
  fi
}

parse_common_flags() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --verbose)
        VERBOSE=true
        shift
        ;;
      --uninstall)
        UNINSTALL=true
        shift
        ;;
      -h|--help)
        SHOW_HELP=true
        shift
        ;;
      *)
        die "Unknown option: $1"
        ;;
    esac
  done
}

link_path() {
  local src="$1"
  local dest="$2"

  if [[ ! -e "$src" ]]; then
    warn "Source does not exist: $src"
    return 1
  fi

  mkdir -p "$(dirname "$dest")"

  if [[ "$UNINSTALL" == "true" ]]; then
    if [[ -e "$dest" || -L "$dest" ]]; then
      run_cmd rm -rf "$dest"
      log_step "Removed: $dest"
    else
      log_verbose "Already absent: $dest"
    fi
    return 0
  fi

  if [[ -e "$dest" || -L "$dest" ]]; then
    run_cmd rm -rf "$dest"
  fi

  if ln -s "$src" "$dest" 2>/dev/null; then
    log_step "Linked: $src -> $dest"
  elif cp -r "$src" "$dest"; then
    warn "Symlink failed; copied instead: $src -> $dest"
  else
    warn "Failed to install: $src -> $dest"
    return 1
  fi
}
