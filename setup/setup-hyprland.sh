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
Usage: ./setup/setup-hyprland.sh [flags...]

Install or remove Hyprland tiling compositor and related packages.

Installs: hyprland, noctalia, fuzzel, slurp, hyprpaper, hypridle,
hyprlock, grim, xdg-desktop-portal-hyprland, hyprpolkitagent.

Also symlinks ~/cmd/configs/hypr -> ~/.config/hypr.

Options:
  --offline    Skip online checks.
  --uninstall  Remove packages and config symlink.
  --verbose    Show extra debug output.
  -h, --help   Show this help message.
USAGE
    exit 0
fi

PACKAGES=(
    hyprland
    noctalia
    fuzzel
    slurp
    hyprpaper
    hypridle
    hyprlock
    grim
    xdg-desktop-portal-hyprland
    hyprpolkitagent
)

if [[ "$UNINSTALL" == "true" ]]; then
    log "Uninstalling Hyprland packages"
    run_cmd sudo pacman -Rns --noconfirm "${PACKAGES[@]}" 2>/dev/null || true

    log "Removing config symlink"
    link_path "$ROOT_DIR/configs/hypr" "$HOME/.config/hypr"

    log "Done"
    exit 0
fi

log "Installing Hyprland packages"
run_cmd sudo pacman -S --needed --noconfirm "${PACKAGES[@]}"

log "Symlinking Hyprland config"
link_path "$ROOT_DIR/configs/hypr" "$HOME/.config/hypr"

log "Done"
