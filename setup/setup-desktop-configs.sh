#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
# shellcheck source=setup/lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"
SHOW_HELP=false
parse_common_flags "$@"

if [[ "$SHOW_HELP" == "true" ]]; then
  cat <<'USAGE'
Usage: ./setup/setup-desktop-configs.sh [--uninstall] [--verbose]
USAGE
  exit 0
fi

log "Managing desktop integrations"

log_step "Firefox policies"
if [[ "$UNINSTALL" == "true" ]]; then
  run_cmd_may_fail sudo rm -f /etc/firefox/policies/policies.json
  log_step "Removed Firefox policy (if present)"
else
  run_cmd sudo mkdir -p /etc/firefox/policies
  run_cmd sudo cp "$ROOT_DIR/configs/firefox/policies.json" /etc/firefox/policies/policies.json
  log_step "Installed Firefox policy"
fi

log_step "Nautilus action"
link_path "$ROOT_DIR/nautilus/ffmpeg-to-mp3" "$HOME/.local/share/nautilus/scripts/Convert To MP3 (ffmpeg)"

log_step "KServices"
if [[ "$UNINSTALL" == "true" ]]; then
  for file in "$ROOT_DIR"/configs/kservices5/*; do
    [[ -e "$file" ]] || continue
    base=$(basename "$file")
    run_cmd_may_fail rm -f "$HOME/.local/share/kservices5/$base"
    log_verbose "Removed $base"
  done
else
  mkdir -p "$HOME/.local/share/kservices5"
  for file in "$ROOT_DIR"/configs/kservices5/*; do
    [[ -e "$file" ]] || continue
    base=$(basename "$file")
    link_path "$file" "$HOME/.local/share/kservices5/$base"
  done
fi

log_step "KDE config"
link_path "$ROOT_DIR/configs/KDE/kuriikwsfilterrc" "$HOME/.config/kuriikwsfilterrc"

log "Done"
