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

log "Managing editor/terminal configs"

log_step "Neovim"
link_path "$ROOT_DIR/configs/nvim" "$HOME/.config/nvim"

log_step "SpaceVim"
if [[ -d "$HOME/.SpaceVim.d" || "$UNINSTALL" == "true" ]]; then
  link_path "$ROOT_DIR/configs/SpaceVim.d/init.toml" "$HOME/.SpaceVim.d/init.toml"
else
  log_verbose "Skipping SpaceVim: ~/.SpaceVim.d not found"
fi

log_step "Alacritty"
if [[ -x "$ROOT_DIR/configs/alacritty/update-themes.sh" && "$UNINSTALL" == "false" ]]; then
  run_cmd "$ROOT_DIR/configs/alacritty/update-themes.sh"
fi
link_path "$ROOT_DIR/configs/alacritty" "$HOME/.config/alacritty"

log_step "VS Code"
link_path "$ROOT_DIR/configs/vscode/settings.json" "$HOME/.config/Code/User/settings.json"

log_step "Chromium flags"
link_path "$ROOT_DIR/configs/chromium-flags.conf" "$HOME/.config/chromium-flags.conf"

log_step "GDB"
link_path "$ROOT_DIR/configs/gdb/.gdbinit" "$HOME/.gdbinit"

log "Done"
