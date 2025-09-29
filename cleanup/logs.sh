#!/bin/bash

trash "$share/imhex/logs"
trash "$share/imhex/backups"

trash "$share/akondi/Akondi.error"
trash "$share/akondi/Akondi.error.old"
trash "$share/akondi_migration_agent"

trash "/var/log/nginx/access.log"
trash "/var/log/pacman.log"
trash "/var/log/haskell-register.log"