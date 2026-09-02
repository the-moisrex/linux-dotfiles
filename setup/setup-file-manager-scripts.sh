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
Usage: ./setup/setup-file-manager-scripts.sh [flags...]

Install or remove context-menu scripts for file managers.

Deploys scripts from context-menu-scripts/ to:
  Nautilus  — ~/.local/share/nautilus/scripts/
  Dolphin   — ~/.local/share/kservices5/ (as .desktop service menus)
  Thunar    — ~/.config/Thunar/scripts/

Options:
  --uninstall  Remove all installed context-menu scripts.
  --verbose    Show extra debug output.
  -h, --help   Show this help message.
USAGE
    exit 0
fi

SCRIPTS_DIR="$ROOT_DIR/context-menu-scripts"

if [[ ! -d "$SCRIPTS_DIR" ]]; then
    die "Scripts directory not found: $SCRIPTS_DIR"
fi

# --- Nautilus ---
log_step "Nautilus scripts"
nautilus_dir="$HOME/.local/share/nautilus/scripts"
if [[ "$UNINSTALL" == "true" ]]; then
    for script in "$SCRIPTS_DIR"/*; do
        [[ -f "$script" ]] || continue
        base=$(basename "$script")
        run_cmd_may_fail rm -f "$nautilus_dir/$base"
        log_verbose "Removed $base"
    done
else
    mkdir -p "$nautilus_dir"
    for script in "$SCRIPTS_DIR"/*; do
        [[ -f "$script" ]] || continue
        base=$(basename "$script")
        link_path "$script" "$nautilus_dir/$base"
    done
fi

# --- Thunar ---
log_step "Thunar scripts"
thunar_dir="$HOME/.config/Thunar/scripts"
if [[ "$UNINSTALL" == "true" ]]; then
    for script in "$SCRIPTS_DIR"/*; do
        [[ -f "$script" ]] || continue
        base=$(basename "$script")
        run_cmd_may_fail rm -f "$thunar_dir/$base"
        log_verbose "Removed $base"
    done
else
    mkdir -p "$thunar_dir"
    for script in "$SCRIPTS_DIR"/*; do
        [[ -f "$script" ]] || continue
        base=$(basename "$script")
        link_path "$script" "$thunar_dir/$base"
    done
fi

# --- Dolphin (KDE service menus) ---
log_step "Dolphin service menus"
kservices_dir="$HOME/.local/share/kservices5"
bin_dir="$HOME/.local/bin"
desktop_prefix="fm-script"
bin_prefix="fm-"

if [[ "$UNINSTALL" == "true" ]]; then
    for desktop in "$kservices_dir"/${desktop_prefix}-*.desktop; do
        [[ -f "$desktop" ]] || continue
        base=$(basename "$desktop")
        run_cmd_may_fail rm -f "$kservices_dir/$base"
        log_verbose "Removed $base"
    done
    for script in "$SCRIPTS_DIR"/*; do
        [[ -f "$script" ]] || continue
        base=$(basename "$script")
        run_cmd_may_fail rm -f "$bin_dir/${bin_prefix}${base}"
        log_verbose "Removed ${bin_prefix}${base}"
    done
else
    mkdir -p "$kservices_dir" "$bin_dir"

    for script in "$SCRIPTS_DIR"/*; do
        [[ -f "$script" ]] || continue
        base=$(basename "$script")
        cmd_name="${bin_prefix}${base}"
        desktop_file="$kservices_dir/${desktop_prefix}-${base}.desktop"

        # Symlink script to ~/.local/bin/ so KDE can execute it by name
        link_path "$script" "$bin_dir/$cmd_name"

        # Parse MimeType from script header (default: all/all)
        mime_type=$(grep -m1 '^# MimeType:' "$script" 2>/dev/null | sed 's/^# MimeType:[[:space:]]*//' || true)
        [[ -z "$mime_type" ]] && mime_type="all/all"

        # Parse Name from script header (default: Run <base>)
        action_name=$(grep -m1 '^# Name:' "$script" 2>/dev/null | sed 's/^# Name:[[:space:]]*//' || true)
        [[ -z "$action_name" ]] && action_name="Run $base"

        # Generate the .desktop file (Exec uses full path)
        cat > "$desktop_file" <<DESKTOP
[Desktop Entry]
Type=Service
MimeType=$mime_type
Actions=runScript
ServiceTypes=KonqPopupMenu/Plugin

[Desktop Action runScript]
Name=$action_name
Icon=utilities-terminal
Exec=$bin_dir/$cmd_name %F
DESKTOP
        chmod +x "$desktop_file"

        log_step "Created: $desktop_file (MimeType=$mime_type)"
    done
fi

log "Done"
