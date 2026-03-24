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
Usage: ./setup/setup-vscode-config.sh [--offline] [--uninstall] [--verbose]
USAGE
    exit 0
fi

install_extensions() {
    local extension_file="$ROOT_DIR/configs/vscode/extensions.txt"
    [[ -f "$extension_file" ]] || return 0
    
    if [[ "$UNINSTALL" == "true" ]]; then
        return 0
    fi
    
    local code_cli=""
    if command -v code >/dev/null 2>&1; then
        code_cli="code"
        elif command -v code-server >/dev/null 2>&1; then
        code_cli="code-server"
    else
        warn "Neither code nor code-server CLI found; skipping extension install"
        return 0
    fi
    
    log "Installing VS Code extensions from $extension_file"
    while IFS= read -r ext; do
        [[ -z "$ext" || "$ext" =~ ^# ]] && continue
        run_cmd_may_fail "$code_cli" --install-extension "$ext" --force
    done < "$extension_file"
}

log "Managing VS Code config"
link_path "$ROOT_DIR/configs/vscode/settings.json" "$HOME/.config/Code/User/settings.json"
# link_path "$ROOT_DIR/configs/vscode/projects.json" "$HOME/.config/Code/User/projects.json"
link_path "$ROOT_DIR/configs/vscode/keybindings.json" "$HOME/.config/Code/User/keybindings.json"
link_path "$ROOT_DIR/configs/vscode/settings.json" "$HOME/.local/share/code-server/User/settings.json"
#link_path "$ROOT_DIR/configs/vscode/projects.json" "$HOME/.local/share/code-server/User/projects.json"
link_path "$ROOT_DIR/configs/vscode/keybindings.json" "$HOME/.local/share/code-server/User/keybindings.json"
if ! $OFFLINE; then
    install_extensions
else
    log "Installing Extensions are ignored."
fi
log "Done"
