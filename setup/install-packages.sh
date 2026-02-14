#!/bin/bash
# Script to detect distribution and install packages from appropriate file

echo "Detecting distribution and installing packages..."

log () {
    echo "  $@"
}

# Detect the Linux distribution
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$NAME
    DISTRO_ID=$ID
elif type lsb_release >/dev/null 2>&1; then
    DISTRO=$(lsb_release -si)
    DISTRO_ID=$(echo $DISTRO | tr '[:upper:]' '[:lower:]')
elif [ -f /etc/debian_version ]; then
    DISTRO="Debian"
    DISTRO_ID="debian"
elif [ -f /etc/redhat-release ]; then
    DISTRO="Red Hat"
    DISTRO_ID="redhat"
else
    DISTRO="Unknown"
    DISTRO_ID="unknown"
fi

log "Detected distribution: $DISTRO ($DISTRO_ID)"

# Determine the package manager and package file to use
case $DISTRO_ID in
    ubuntu|debian|mint|pop)
        PACKAGE_MANAGER="apt"
        PKG_FILE="../../pkgs/ubuntu.txt"
        UPDATE_CMD="sudo apt update"
        INSTALL_CMD="sudo apt install -y"
        ;;
    fedora|rhel|centos|rocky|almalinux)
        PACKAGE_MANAGER="dnf"
        PKG_FILE="../../pkgs/fedora.txt"
        UPDATE_CMD="sudo dnf check-update || true"
        INSTALL_CMD="sudo dnf install -y"
        ;;
    opensuse*|suse)
        PACKAGE_MANAGER="zypper"
        PKG_FILE="../../pkgs/opensuse.txt"
        UPDATE_CMD="sudo zypper refresh"
        INSTALL_CMD="sudo zypper install -y"
        ;;
    arch|manjaro)
        PACKAGE_MANAGER="pacman"
        PKG_FILE="../../pkgs/arch.txt"
        UPDATE_CMD="sudo pacman -Sy"
        INSTALL_CMD="sudo pacman -S --noconfirm"
        ;;
    *)
        log "Unsupported distribution: $DISTRO_ID"
        exit 1
        ;;
esac

log "Using package manager: $PACKAGE_MANAGER"

# Check if the package file exists for this distribution
if [ ! -f "$PKG_FILE" ]; then
    log "$PKG_FILE not found, skipping package installation for $DISTRO_ID."
    exit 0
fi

# Update package list
log "Updating package list..."
eval "$UPDATE_CMD"

# Read packages from file and install them
log "Reading packages from $PKG_FILE..."
while IFS= read -r package || [ -n "$package" ]; do
    # Skip empty lines and comments
    if [[ -z "$package" || "$package" =~ ^[[:space:]]*# ]]; then
        continue
    fi
    
    # Remove leading/trailing whitespace
    package=$(echo "$package" | xargs)
    
    log "Installing package: $package"
    eval "$INSTALL_CMD $package"
done < "$PKG_FILE"

log "Package installation for $DISTRO_ID completed!"