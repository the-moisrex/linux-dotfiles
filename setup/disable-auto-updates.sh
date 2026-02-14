#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=setup/lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"
SHOW_HELP=false
parse_common_flags "$@"

if [[ "$SHOW_HELP" == "true" ]]; then
  cat <<'USAGE'
Usage: ./setup/disable-auto-updates.sh [--uninstall] [--verbose]
  --uninstall  Re-enable automatic updates (best effort)
USAGE
  exit 0
fi

log "Managing automatic update settings"

if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  DISTRO_ID=${ID}
else
  DISTRO_ID="unknown"
fi
log_step "Detected distribution: $DISTRO_ID"

set_apt_value() {
  local key="$1"
  local value="$2"
  local file="$3"
  [[ -f "$file" ]] || return 0
  run_cmd sudo sed -i "s|^\s*${key}.*|${key} \"${value}\";|" "$file"
}

case "$DISTRO_ID" in
  ubuntu|debian|mint|pop)
    if [[ "$UNINSTALL" == "true" ]]; then
      log_step "Re-enabling unattended upgrades (best effort)"
      set_apt_value 'APT::Periodic::Update-Package-Lists' '1' /etc/apt/apt.conf.d/20auto-upgrades
      set_apt_value 'APT::Periodic::Unattended-Upgrade' '1' /etc/apt/apt.conf.d/20auto-upgrades
      run_cmd_may_fail sudo systemctl enable --now unattended-upgrades
    else
      log_step "Disabling unattended upgrades"
      set_apt_value 'APT::Periodic::Update-Package-Lists' '0' /etc/apt/apt.conf.d/20auto-upgrades
      set_apt_value 'APT::Periodic::Unattended-Upgrade' '0' /etc/apt/apt.conf.d/20auto-upgrades
      set_apt_value 'APT::Periodic::Update-Package-Lists' '0' /etc/apt/apt.conf.d/10periodic
      set_apt_value 'APT::Periodic::Download-Upgradeable-Packages' '0' /etc/apt/apt.conf.d/10periodic
      set_apt_value 'APT::Periodic::AutocleanInterval' '0' /etc/apt/apt.conf.d/10periodic
      run_cmd_may_fail sudo systemctl disable --now unattended-upgrades
    fi
    ;;
  fedora|rhel|centos|rocky|almalinux)
    if [[ -f /etc/dnf/automatic.conf ]]; then
      if [[ "$UNINSTALL" == "true" ]]; then
        log_step "Re-enabling dnf automatic updates"
        run_cmd sudo sed -i 's/^apply_updates\s*=.*/apply_updates = yes/' /etc/dnf/automatic.conf
        run_cmd_may_fail sudo systemctl enable --now dnf-automatic.timer
      else
        log_step "Disabling dnf automatic updates"
        run_cmd sudo sed -i 's/^apply_updates\s*=.*/apply_updates = no/' /etc/dnf/automatic.conf
        run_cmd_may_fail sudo systemctl disable --now dnf-automatic.timer
      fi
    fi
    ;;
  arch|manjaro)
    if [[ "$UNINSTALL" == "true" ]]; then
      log_step "Re-enabling known pacman timers"
      run_cmd_may_fail sudo systemctl enable --now pacman.timer
      run_cmd_may_fail sudo systemctl enable --now pacman-filesdb-refresh.timer
    else
      log_step "Disabling known pacman timers"
      run_cmd_may_fail sudo systemctl disable --now pacman.timer
      run_cmd_may_fail sudo systemctl disable --now pacman-filesdb-refresh.service
      run_cmd_may_fail sudo systemctl disable --now pacman-filesdb-refresh.timer
    fi
    ;;
  *)
    warn "Unsupported distribution: $DISTRO_ID"
    ;;
esac

log "Done"
