#!/bin/bash
# Script to configure sudo without requiring a password for the current user

echo "Configuring sudo without password..."

# Check if running as root
if [[ $EUID -eq 0 ]]; then
   echo "  This script should not be run as root" 
   exit 1
fi

USER_NAME=$(whoami)
SUDO_FILE="/etc/sudoers.d/${USER_NAME}-nopasswd"

# Create a temporary file with the sudoers entry
TEMP_FILE=$(mktemp)
echo "${USER_NAME} ALL=(ALL) NOPASSWD: ALL" > "$TEMP_FILE"

# Validate the sudoers file syntax
if visudo -c -f "$TEMP_FILE" > /dev/null 2>&1; then
    # Copy the validated file to the sudoers directory
    sudo cp "$TEMP_FILE" "$SUDO_FILE"
    sudo chmod 440 "$SUDO_FILE"
    echo "  Successfully configured sudo without password for user: $USER_NAME"
else
    echo "  Error: Invalid sudoers file syntax"
    rm "$TEMP_FILE"
    exit 1
fi

# Clean up
rm "$TEMP_FILE"

echo "  Sudo configuration completed!"