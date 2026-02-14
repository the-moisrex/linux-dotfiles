#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
# shellcheck source=setup/lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"
SHOW_HELP=false
PROFILE_PATH=""
REMAINING_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      shift
      [[ $# -gt 0 ]] || die "--profile expects a value"
      PROFILE_PATH="$1"
      shift
      ;;
    *)
      REMAINING_ARGS+=("$1")
      shift
      ;;
  esac
done

parse_common_flags "${REMAINING_ARGS[@]}"

if [[ "$SHOW_HELP" == "true" ]]; then
  cat <<'USAGE'
Usage: ./setup/setup-firefox-userchrome.sh [--profile <profile-path>] [--uninstall] [--verbose]
  --profile  Absolute path to a Firefox profile directory.
             If omitted, the default profile from ~/.mozilla/firefox/profiles.ini is used.
USAGE
  exit 0
fi

profiles_ini="$HOME/.mozilla/firefox/profiles.ini"

resolve_default_profile() {
  [[ -f "$profiles_ini" ]] || return 1

  local profile_root
  profile_root=$(dirname "$profiles_ini")

  python - "$profiles_ini" "$profile_root" <<'PY'
import configparser
import os
import sys

ini_path = sys.argv[1]
profile_root = sys.argv[2]
config = configparser.RawConfigParser()
config.read(ini_path)

selected = None
for section in config.sections():
    if not section.startswith("Profile"):
        continue
    if config.get(section, "Default", fallback="0") == "1":
        selected = section
        break
if selected is None:
    for section in config.sections():
        if section.startswith("Install"):
            default_ref = config.get(section, "Default", fallback="")
            if default_ref:
                selected = default_ref if config.has_section(default_ref) else None
                break
if selected is None:
    for section in config.sections():
        if section.startswith("Profile"):
            selected = section
            break
if selected is None:
    sys.exit(1)

path = config.get(selected, "Path", fallback="")
is_relative = config.get(selected, "IsRelative", fallback="1") == "1"
if not path:
    sys.exit(1)

if is_relative:
    print(os.path.join(profile_root, path))
else:
    print(path)
PY
}

if [[ -z "$PROFILE_PATH" ]]; then
  if ! PROFILE_PATH=$(resolve_default_profile); then
    die "Could not determine Firefox default profile; pass --profile explicitly"
  fi
fi

if [[ ! -d "$PROFILE_PATH" ]]; then
  die "Firefox profile directory does not exist: $PROFILE_PATH"
fi

chrome_dir="$PROFILE_PATH/chrome"
css_src="$ROOT_DIR/configs/firefox/userChrome.css"
css_dest="$chrome_dir/userChrome.css"

log "Managing Firefox userChrome.css"
log_step "Profile: $PROFILE_PATH"

if [[ "$UNINSTALL" == "true" ]]; then
  run_cmd_may_fail rm -f "$css_dest"
  log_step "Removed: $css_dest"
else
  run_cmd mkdir -p "$chrome_dir"
  link_path "$css_src" "$css_dest"
fi

log "Done"
