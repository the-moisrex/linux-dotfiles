#!/bin/bash

trash "$share/imhex/logs"
trash "$share/imhex/backups"

trash "$share/akondi/Akondi.error"
trash "$share/akondi/Akondi.error.old"
trash "$share/akondi_migration_agent"

trash "/var/log/nginx/access.log"
trash "/var/log/pacman.log"
trash "/var/log/haskell-register.log"

trash "$HOME/.wget-hsts"
trash "$HOME/.cargo"
trash "$HOME/.npm"
trash "$HOME/.conan2"
trash "$HOME/.parallel"
trash "$HOME/.icons"
# trash "$HOME/.var"  # flatpak?
trash "$HOME/.fontconfig"
# trash "$HOME/.java" # contains font configs, I think
trash "$HOME/.swt" # extracted native libraries (.so files) which are essentially a cache
trash "$HOME/.ipython"
# trash "$HOME/.skiko" # cache for unpacked native libraries for Jetbrains
trash "$HOME/.shutter"
trash "$HOME/.fltk"
trash "$HOME/.designer"
trash "$HOME/.codex/log"
