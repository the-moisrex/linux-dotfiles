# Setup Scripts

A collection of scripts to configure a Linux desktop with common settings.

## Overview

This directory contains a set of configuration scripts designed to automate common desktop setup tasks. Each script performs a specific configuration task independently.

## Directory Structure

```
setup/
├── configure-sudo.sh         # Configure passwordless sudo
├── install-packages.sh       # Install packages from pkgs directory
├── disable-screen-lock.sh    # Disable auto-lock and screen power management
├── disable-auto-updates.sh   # Disable automatic updates
└── README.md                 # This file
```

## Features

### 1. Passwordless Sudo (`configure-sudo.sh`)
- Configures sudo to work without requiring a password for the current user
- Adds a specific entry to `/etc/sudoers.d/` for security
- Validates the sudoers file syntax before applying changes

### 2. Package Installation (`install-packages.sh`)
- Automatically detects the Linux distribution
- Installs packages from the appropriate file in the `pkgs/` directory:
  - `pkgs/ubuntu.txt` for Ubuntu/Debian systems
  - `pkgs/fedora.txt` for Fedora/RHEL/CentOS systems
  - `pkgs/opensuse.txt` for openSUSE systems
  - `pkgs/arch.txt` for Arch Linux systems
- Uses the appropriate package manager (apt, dnf, zypper, pacman)

### 3. Disable Screen Lock (`disable-screen-lock.sh`)
- Disables screensaver and automatic locking
- Prevents screen from turning off automatically
- Supports GNOME, KDE Plasma, and XFCE desktop environments
- Configures systemd-logind settings
- Disables DPMS (Energy Star) features

### 4. Disable Auto Updates (`disable-auto-updates.sh`)
- Disables automatic system updates
- Works with Ubuntu/Debian, Fedora/RHEL/CentOS, openSUSE, and Arch systems
- Stops auto-update services and timers
- Modifies distribution-specific configuration files

## Usage

Run individual scripts as needed:

```bash
# Make the script executable
chmod +x setup/script-name.sh

# Run the script
./setup/script-name.sh
```

Or run from anywhere if you're in the project root:

```bash
./setup/configure-sudo.sh
./setup/install-packages.sh
./setup/disable-screen-lock.sh
./setup/disable-auto-updates.sh
```

## Requirements

- Bash shell
- Sudo privileges (for most operations)
- Desktop environment (for screen lock settings)

## Notes

- Always review the scripts before running them on production systems
- The package installation script looks for package lists in the `pkgs/` directory relative to the project root
- Each script can be run independently based on your needs