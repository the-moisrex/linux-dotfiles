#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=setup/lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"
SHOW_HELP=false
parse_common_flags "$@"

if [[ "$SHOW_HELP" == "true" ]]; then
  cat <<'USAGE'
Usage: ./setup/disable-screen-lock.sh [--uninstall] [--verbose]
  --uninstall  Restore common lock/power defaults (best effort)
USAGE
  exit 0
fi

log "Managing screen lock and power settings"

set_gsetting() {
  local key="$1"
  local value="$2"
  if command -v gsettings >/dev/null 2>&1; then
    run_cmd_may_fail gsettings set "${key% *}" "${key##* }" "$value"
  fi
}

if [[ "$UNINSTALL" == "true" ]]; then
  log_step "Restoring desktop lock defaults (best effort)"
  set_gsetting "org.gnome.desktop.screensaver lock-enabled" true
  set_gsetting "org.gnome.desktop.session idle-delay" 300
  set_gsetting "org.gnome.settings-daemon.plugins.power sleep-display-ac" 900
  set_gsetting "org.gnome.settings-daemon.plugins.power sleep-display-battery" 300
  if command -v xset >/dev/null 2>&1; then
    run_cmd_may_fail xset +dpms
    run_cmd_may_fail xset s on
    run_cmd_may_fail xset s blank
  fi
  if [[ -f /etc/systemd/logind.conf ]]; then
    replace_or_append_kv /etc/systemd/logind.conf HandleLidSwitch '=suspend'
    replace_or_append_kv /etc/systemd/logind.conf HandleLidSwitchDocked '=ignore'
    run_cmd_may_fail sudo systemctl restart systemd-logind
  fi
else
  log_step "Disabling lock and display sleep"
  set_gsetting "org.gnome.desktop.screensaver lock-enabled" false
  set_gsetting "org.gnome.desktop.session idle-delay" 0
  set_gsetting "org.gnome.settings-daemon.plugins.power sleep-display-ac" 0
  set_gsetting "org.gnome.settings-daemon.plugins.power sleep-display-battery" 0
  if command -v xset >/dev/null 2>&1; then
    run_cmd_may_fail xset s off
    run_cmd_may_fail xset s noblank
    run_cmd_may_fail xset -dpms
  fi
  if [[ -f /etc/systemd/logind.conf ]]; then
    replace_or_append_kv /etc/systemd/logind.conf HandleLidSwitch '=ignore'
    replace_or_append_kv /etc/systemd/logind.conf HandleLidSwitchDocked '=ignore'
    run_cmd_may_fail sudo systemctl restart systemd-logind
  fi
fi

if command -v kwriteconfig5 >/dev/null 2>&1; then
  log_step "Applying KDE settings"
  if [[ "$UNINSTALL" == "true" ]]; then
    run_cmd_may_fail kwriteconfig5 --file "$HOME/.config/kscreenlockerrc" --group Daemon --key Autolock true
  else
    run_cmd_may_fail kwriteconfig5 --file "$HOME/.config/kscreenlockerrc" --group Daemon --key Autolock false
    run_cmd_may_fail kwriteconfig5 --file "$HOME/.config/powermanagementprofilesrc" --group AC --group DimDisplay --key idleTime 0
  fi
fi

if command -v xfconf-query >/dev/null 2>&1; then
  log_step "Applying XFCE settings"
  if [[ "$UNINSTALL" == "true" ]]; then
    run_cmd_may_fail xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/dpms-enabled -s true
  else
    run_cmd_may_fail xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/dpms-enabled -s false
    run_cmd_may_fail xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/blank-on-ac -s false
  fi
fi

log "Done"
