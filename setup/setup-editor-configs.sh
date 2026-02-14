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
Usage: ./setup/setup-editor-configs.sh [--uninstall] [--verbose]
USAGE
  exit 0
fi

log "Managing editor configs"

log_step "Neovim"
link_path "$ROOT_DIR/configs/nvim" "$HOME/.config/nvim"

log_step "SpaceVim"
if [[ -d "$HOME/.SpaceVim.d" || "$UNINSTALL" == "true" ]]; then
  link_path "$ROOT_DIR/configs/SpaceVim.d/init.toml" "$HOME/.SpaceVim.d/init.toml"
else
  log_verbose "Skipping SpaceVim: ~/.SpaceVim.d not found"
fi

log "Done"
