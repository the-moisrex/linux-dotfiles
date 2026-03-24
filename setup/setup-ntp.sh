#!/usr/bin/env bash
# todo: add uninstall

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
# shellcheck source=setup/lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"
SHOW_HELP=false
parse_common_flags "$@"

if [[ "$SHOW_HELP" == "true" ]]; then
  cat <<'USAGE'
Usage: ./setup/setup-ntp.sh [--verbose]
USAGE
    exit 0
fi


# NTP servers to configure
NTP_SERVERS="ntp.day.ir ntp.meetbsd.ir 0.arch.pool.ntp.org 1.arch.pool.ntp.org 2.arch.pool.ntp.org 3.arch.pool.ntp.org"

log "Managing NTP Protocol (for time and date)"

# Configure systemd timesyncd (modern systems)
if [ -f /etc/systemd/timesyncd.conf ]; then
    log_step "Configure systemd timesyncd (modern systems)"
    run_cmd sudo sed -i -r 's/^(#\s*)?FallbackNTP=.*/FallbackNTP='"$NTP_SERVERS"'/' /etc/systemd/timesyncd.conf
    run_cmd sudo timedatectl set-ntp 0
    run_cmd sudo timedatectl set-ntp 1
    log_step "✓ systemd timesyncd updated"
else
    log_step "timesyncd is not installed."
fi

# Configure ntpd (older Debian/Ubuntu/RHEL)
if [ -f /etc/ntp.conf ]; then
    log_step "Configure ntpd (older Debian/Ubuntu/RHEL)"
    run_cmd sudo sed -i '/^server /d' /etc/ntp.conf
    for server in $NTP_SERVERS; do
        echo "server $server" | sudo tee -a /etc/ntp.conf > /dev/null
    done
    run_cmd sudo systemctl restart ntpd
    log_step "✓ ntpd updated"
else
    log_step "ntpd is not installed."
fi

# Configure chrony (Ubunut/Fedora/RHEL/CentOS)
if [ -f /etc/chrony.conf ]; then
    log_step "Configure chrony (Ubuntu/Fedora/RHEL/CentOS)"
    run_cmd sudo sed -i '/^server /d' /etc/chrony.conf
    if [ -f /etc/chrony/sources.d/ ]; then
        chrony_config="/etc/chrony/sources.d/users-ntp-servers.conf"
        for server in $NTP_SERVERS; do
            if grep -q "$server" "$chrony_config"; then
                log_step "skipped $server, it's already in the $chrony_config"
            else
                echo "server $server" iburst | sudo tee -a "$chrony_config" > /dev/null
                log_step "Added $server to $chrony_config"
            fi
        done
    else
        chrony_config="/etc/chrony.conf"
        for server in $NTP_SERVERS; do
            if grep -q "$server" "$chrony_config"; then
                log_step "skipped $server, it's already in the $chrony_config"
            else
                echo "server $server" iburst | sudo tee -a "$chrony_config" > /dev/null
                log_step "Added $server to $chrony_config"
            fi
        done
    fi
    run_cmd sudo systemctl restart chronyd
    log_step "✓ chrony updated"
else
    log_step "chrony is not installed."
fi

# Final check
if ! command -v timedatectl &> /dev/null && ! command -v systemctl &> /dev/null; then
    warn_step "No NTP service detected. Manual configuration required."
fi

log "Done"
