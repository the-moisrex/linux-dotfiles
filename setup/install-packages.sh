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
Usage: ./setup/install-packages.sh [--uninstall] [--verbose]
  --uninstall  Remove listed packages instead of installing them
USAGE
  exit 0
fi

log "Managing package installation"

if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  DISTRO_ID=${ID}
else
  DISTRO_ID="unknown"
fi
log_step "Detected distribution: $DISTRO_ID"

PACKAGE_FILE=""
UPDATE_CMD=()
ACTION_CMD=()

case "$DISTRO_ID" in
  arch|manjaro)
    PACKAGE_FILE="$ROOT_DIR/pkgs/pacman-core.txt"
    UPDATE_CMD=(sudo pacman -Sy)
    if [[ "$UNINSTALL" == "true" ]]; then
      ACTION_CMD=(sudo pacman -Rns --noconfirm)
    else
      ACTION_CMD=(sudo pacman -S --noconfirm --needed)
    fi
    ;;
  fedora|rhel|centos|rocky|almalinux)
    PACKAGE_FILE="$ROOT_DIR/pkgs/dnf-core.txt"
    UPDATE_CMD=(sudo dnf makecache)
    if [[ "$UNINSTALL" == "true" ]]; then
      ACTION_CMD=(sudo dnf remove -y)
    else
      ACTION_CMD=(sudo dnf install -y)
    fi
    ;;
  *)
    warn "No supported package list for distribution: $DISTRO_ID"
    exit 0
    ;;
esac

if [[ ! -f "$PACKAGE_FILE" ]]; then
  warn "Package file does not exist: $PACKAGE_FILE"
  exit 0
fi

log_step "Using package list: $PACKAGE_FILE"
run_cmd "${UPDATE_CMD[@]}"

while IFS= read -r package || [[ -n "$package" ]]; do
  [[ -z "$package" || "$package" =~ ^[[:space:]]*# ]] && continue
  package=$(echo "$package" | xargs)
  [[ -z "$package" ]] && continue
  if [[ "$UNINSTALL" == "true" ]]; then
    log_step "Removing package: $package"
  else
    log_step "Installing package: $package"
  fi
  run_cmd_may_fail "${ACTION_CMD[@]}" "$package"
done < "$PACKAGE_FILE"

log "Done"
