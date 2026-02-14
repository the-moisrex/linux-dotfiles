#!/bin/bash
# Script to disable automatic updates based on the detected distribution

echo "Disabling automatic updates..."

log () {
    echo "  $@"
}

# Detect the Linux distribution
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO_ID=$ID
elif type lsb_release >/dev/null 2>&1; then
    DISTRO_ID=$(lsb_release -si | tr '[:upper:]' '[:lower:]')
elif [ -f /etc/debian_version ]; then
    DISTRO_ID="debian"
elif [ -f /etc/redhat-release ]; then
    DISTRO_ID="redhat"
else
    DISTRO_ID="unknown"
fi

log "Detected distribution: $DISTRO_ID"

# Disable automatic updates based on the distribution
case $DISTRO_ID in
    ubuntu|debian|mint|pop)
        # For Ubuntu/Debian systems
        if command -v apt &> /dev/null; then
            # Disable unattended upgrades
            if [ -f /etc/apt/apt.conf.d/20auto-upgrades ]; then
                sudo sed -i 's/APT::Periodic::Update-Package-Lists.*/APT::Periodic::Update-Package-Lists "0";/' /etc/apt/apt.conf.d/20auto-upgrades
                sudo sed -i 's/APT::Periodic::Unattended-Upgrade.*/APT::Periodic::Unattended-Upgrade "0";/' /etc/apt/apt.conf.d/20auto-upgrades
            fi
            
            # Also check for other auto-update configs
            if [ -f /etc/apt/apt.conf.d/10periodic ]; then
                sudo sed -i 's/APT::Periodic::Update-Package-Lists.*/APT::Periodic::Update-Package-Lists "0";/' /etc/apt/apt.conf.d/10periodic
                sudo sed -i 's/APT::Periodic::Download-Upgradeable-Packages.*/APT::Periodic::Download-Upgradeable-Packages "0";/' /etc/apt/apt.conf.d/10periodic
                sudo sed -i 's/APT::Periodic::AutocleanInterval.*/APT::Periodic::AutocleanInterval "0";/' /etc/apt/apt.conf.d/10periodic
            fi
            
            # Disable the unattended-upgrades service if it exists
            sudo systemctl disable --now unattended-upgrades 2>/dev/null || true
            
            log "Disabled automatic updates for Ubuntu/Debian system"
        fi
        ;;
    fedora|rhel|centos|rocky|almalinux)
        # For Fedora/RHEL/CentOS systems
        if command -v dnf &> /dev/null; then
            # Modify dnf automatic configuration
            if [ -f /etc/dnf/automatic.conf ]; then
                sudo sed -i 's/apply_updates.*/apply_updates = no/' /etc/dnf/automatic.conf
                sudo sed -i 's/download_updates.*/download_updates = no/' /etc/dnf/automatic.conf
            fi
            
            # Disable the dnf-automatic timer and service
            sudo systemctl disable --now dnf-automatic.timer 2>/dev/null || true
            sudo systemctl disable --now dnf-automatic-install.timer 2>/dev/null || true
            sudo systemctl disable --now dnf-automatic.service 2>/dev/null || true
            sudo systemctl disable --now dnf-automatic-install.service 2>/dev/null || true
            
            log "Disabled automatic updates for Fedora/RHEL/CentOS system"
        fi
        ;;
    opensuse*|suse)
        # For openSUSE systems
        if command -v zypper &> /dev/null; then
            # Disable YaST online update service
            sudo systemctl disable --now yast2-online-update-configure.timer 2>/dev/null || true
            sudo systemctl disable --now yast2-online-update-configure.service 2>/dev/null || true
            
            # Modify zypper configuration to disable auto refresh
            if [ -f /etc/zypp/zypp.conf ]; then
                # Backup original config
                sudo cp /etc/zypp/zypp.conf /etc/zypp/zypp.conf.backup
                
                # Comment out or set to 0 the refresh interval
                sudo sed -i 's/^#*commit\.downloadMode.*/commit.downloadMode = manual/' /etc/zypp/zypp.conf
                sudo sed -i 's/^#*refresh\.delay.*/refresh.delay = 0/' /etc/zypp/zypp.conf
            fi
            
            log "Disabled automatic updates for openSUSE system"
        fi
        ;;
    arch|manjaro)
        # For Arch Linux systems
        # Arch doesn't have automatic updates by default, but if a service is set up
        sudo systemctl disable --now pacman.timer 2>/dev/null || true
        sudo systemctl disable --now pacman-mirrors.timer 2>/dev/null || true
        sudo systemctl disable --now pacman-filesdb-refresh.service 2>/dev/null || true
        sudo systemctl disable --now pacman-filesdb-refresh.timer 2>/dev/null || true
        
        # If using an AUR helper with auto-update features, those would need to be configured separately
        log "Disabled automatic updates for Arch Linux system (if any were enabled)"
        ;;
    *)
        log "Unsupported distribution: $DISTRO_ID. Automatic updates may not be disabled."
        ;;
esac

log "Automatic updates have been disabled!"