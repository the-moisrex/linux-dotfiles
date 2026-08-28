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
Usage: ./setup/setup-alacritty-config.sh [flags...]

Install or remove Alacritty terminal emulator config.

Optionally fetches updated themes (skipped with --offline).

Options:
  --offline    Skip fetching Alacritty themes.
  --uninstall  Remove the Alacritty config symlink.
  --verbose    Show extra debug output.
  -h, --help   Show this help message.
USAGE
    exit 0
fi

log "Managing Alacritty config"

if [[ -x "$ROOT_DIR/configs/alacritty/update-themes.sh" && "$UNINSTALL" == "false" ]]; then
    if $OFFLINE; then
        warn_step "Updating Alacrity Themes Ignored."
    else
        run_cmd "$ROOT_DIR/configs/alacritty/update-themes.sh"
    fi
fi

link_path "$ROOT_DIR/configs/alacritty" "$HOME/.config/alacritty"

log "Done"
