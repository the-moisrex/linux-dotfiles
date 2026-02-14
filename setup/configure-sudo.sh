#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=setup/lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"
SHOW_HELP=false
parse_common_flags "$@"

if [[ "$SHOW_HELP" == "true" ]]; then
  cat <<'USAGE'
Usage: ./setup/configure-sudo.sh [--uninstall] [--verbose]
USAGE
  exit 0
fi

log "Configuring passwordless sudo"

if [[ $EUID -eq 0 ]]; then
  die "This script should not be run as root"
fi

USER_NAME=$(whoami)
SUDO_FILE="/etc/sudoers.d/${USER_NAME}-nopasswd"

if [[ "$UNINSTALL" == "true" ]]; then
  log_step "Removing sudoers override: $SUDO_FILE"
  if [[ -f "$SUDO_FILE" ]]; then
    run_cmd sudo rm -f "$SUDO_FILE"
    log_step "Removed"
  else
    log_step "Nothing to remove"
  fi
  log "Done"
  exit 0
fi

TEMP_FILE=$(mktemp)
trap 'rm -f "$TEMP_FILE"' EXIT
printf '%s ALL=(ALL) NOPASSWD: ALL\n' "$USER_NAME" > "$TEMP_FILE"

log_step "Validating generated sudoers file"
if visudo -c -f "$TEMP_FILE" >/dev/null 2>&1; then
  log_step "Installing $SUDO_FILE"
  run_cmd sudo cp "$TEMP_FILE" "$SUDO_FILE"
  run_cmd sudo chmod 440 "$SUDO_FILE"
  log_step "Configured for user: $USER_NAME"
else
  die "Generated sudoers entry is invalid"
fi

log "Done"
