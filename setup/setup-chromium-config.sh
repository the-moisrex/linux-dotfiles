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
Usage: ./setup/setup-chromium-config.sh [--uninstall] [--verbose]
USAGE
  exit 0
fi

log "Managing Chromium config"
link_path "$ROOT_DIR/configs/chromium-flags.conf" "$HOME/.config/chromium-flags.conf"
log "Done"
