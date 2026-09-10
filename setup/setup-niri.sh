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
Usage: ./setup/setup-niri.sh [flags...]

Install or remove Niri scrollable tiling compositor and related packages.

Installs: niri, xwayland-satellite.

Also symlinks ~/cmd/configs/niri -> ~/.config/niri.

Options:
  --offline    Skip online checks.
  --uninstall  Remove packages and config symlink.
  --verbose    Show extra debug output.
  -h, --help   Show this help message.
USAGE
    exit 0
fi

PACKAGES=(
    niri
    xwayland-satellite
)

if [[ "$UNINSTALL" == "true" ]]; then
    log "Uninstalling Niri packages"
    run_cmd sudo pacman -Rns --noconfirm "${PACKAGES[@]}" 2>/dev/null || true

    log "Removing config symlink"
    link_path "$ROOT_DIR/configs/niri" "$HOME/.config/niri"

    log "Done"
    exit 0
fi

log "Installing Niri packages"
run_cmd sudo pacman -S --needed --noconfirm "${PACKAGES[@]}"

log "Symlinking Niri config"
link_path "$ROOT_DIR/configs/niri" "$HOME/.config/niri"

log "Done"
