#!/bin/bash
# Script to disable auto-lock and screen power management

echo "Disabling auto-lock and screen power management..."

log () {
    echo "  $@"
}


# Disable screensaver and screen locking
gsettings set org.gnome.desktop.screensaver lock-enabled false
gsettings set org.gnome.desktop.session idle-delay 0
gsettings set org.gnome.settings-daemon.plugins.power sleep-display-ac 0
gsettings set org.gnome.settings-daemon.plugins.power sleep-display-battery 0

# Disable screen blanking via xset
xset s off
xset s noblank

# Disable DPMS (Energy Star) features
xset -dpms

# For systemd-logind configuration (affects all users)
if [ -f /etc/systemd/logind.conf ]; then
    sudo sed -i 's/#HandleLidSwitch=.*/HandleLidSwitch=ignore/' /etc/systemd/logind.conf
    sudo sed -i 's/#HandleLidSwitchDocked=.*/HandleLidSwitchDocked=ignore/' /etc/systemd/logind.conf
    sudo systemctl restart systemd-logind
fi

# For lightdm configuration (if installed)
if [ -f /etc/lightdm/lightdm.conf ]; then
    sudo sed -i '/^\[Seat:\*\]$/,/^$/ { /xserver-command=/ s/$/ -s 0 -dpms/ }' /etc/lightdm/lightdm.conf
fi

# For KDE Plasma (if installed)
if command -v kwriteconfig5 &> /dev/null; then
    kwriteconfig5 --file "$HOME/.config/kscreenlockerrc" --group "Daemon" --key "Autolock" "false"
    kwriteconfig5 --file "$HOME/.config/powermanagementprofilesrc" --group "AC" --group "DimDisplay" --key "idleTime" "0"
fi

# For XFCE (if installed)
if command -v xfconf-query &> /dev/null; then
    xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/dpms-enabled -s false
    xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/blank-on-ac -s false
    xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/dpms-on-ac-sleep -s 0
    xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/dpms-on-ac-off -s 0
fi

log "Auto-lock and screen power management disabled!"