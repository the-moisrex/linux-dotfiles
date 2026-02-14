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
Usage: ./setup/setup-shell-configs.sh [--uninstall] [--verbose]
USAGE
  exit 0
fi

log "Managing shell configs"

log_step "Fish config"
link_path "$ROOT_DIR/shell/aliases.fish" "$HOME/.config/fish/aliases.fish"
link_path "$ROOT_DIR/shell/config.fish" "$HOME/.config/fish/config.fish"
link_path "$ROOT_DIR/shell/cmd_timer.fish" "$HOME/.config/fish/cmd_timer.fish"
link_path "$ROOT_DIR/shell/completions.fish" "$HOME/.config/fish/completions/completions.fish"
link_path "$ROOT_DIR/assets/ok.oga" "$HOME/.config/fish/assets/ok.oga"
link_path "$ROOT_DIR/assets/error.oga" "$HOME/.config/fish/assets/error.oga"

log_step "Nushell config"
link_path "$ROOT_DIR/shell/aliases.nu" "$HOME/.config/nushell/aliases.nu"
link_path "$ROOT_DIR/shell/config.nu" "$HOME/.config/nushell/config.nu"
link_path "$ROOT_DIR/shell/env.nu" "$HOME/.config/nushell/env.nu"
link_path "$ROOT_DIR/shell/completions.nu" "$HOME/.config/nushell/completions.nu"
link_path "$ROOT_DIR/assets/ok.oga" "$HOME/.config/nushell/assets/ok.oga"
link_path "$ROOT_DIR/assets/error.oga" "$HOME/.config/nushell/assets/error.oga"

log "Done"
