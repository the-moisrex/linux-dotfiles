#!/bin/bash

trash "$share/konsole"

# akonadi
trash "$share/akonadi"
trash "$share/akonadi_migration_agent"

# Privacy: KDE Connect
trash "$share/kontact"
trash "$share/kpeople"
trash "$share/kpeoplevcard"
trash "$share/recently-used.xbel"
trash "$share/kcookiejar"

# Less used apps
trash "$share/kget"
trash "$share/kalgebra"
trash "$share/DisplayCAL"
trash "$share/KDE/angelfish"
trash "$share/KDE/audiotube"
trash "$share/KDE/kasts"
trash "$share/KDE/neochat"
trash "$share/KDE/telly-skout"
trash "$share/telly-skout"
trash "$share/KDE/alligator"
trash "$share/calibre-ebook.com"
trash "$share/baloo"
trash "$share/kate"
trash "$share/kdenlive"
trash "$share/kdevappwizard" # Data for KDevelop app wizard; safe if unused.
trash "$share/kdevelop" # Data for KDevelop IDE; safe if unused.
trash "$share/kongress" # Data for Kongress conference app; safe if unused.
trash "$share/konqueror" # Data for Konqueror browser; safe if unused.
trash "$share/kontrast" # Data for Kontrast color contrast checker; safe if unused.
trash "$share/krdc" # Data for KRDC remote desktop; safe if unused.
trash "$share/krdpserver" # Data for KRDP server; safe if unused.
trash "$share/ktouch" # Data for KTouch typing tutor; safe if unused.
trash "$share/kuiviewer" # Data for KUiviewer; safe if unused.
trash "$share/kwrite" # Data for KWrite editor; safe if unused.
trash "$share/lokalize" # Data for Lokalize translation tool; safe if unused.
trash "$share/partitionmanager" # Data for KDE Partition Manager; safe if unused.


# Krita
trash "$share/krita"
trash "$share/krita.log"
trash "$share/krita-sysinfo.log"
trash "$share/kcachegrind"

# KDE Stuff
trash "$share/plasma_notes"
trash "$share/ktorrent"
trash "$share/gegl-0.4" # Cache and plug-ins for GEGL library (used by GIMP etc.); safe to remove, regenerates as needed.
trash "$share/klipper" # Clipboard history for Klipper; important if used.
trash "$share/kmag" # Data for KMag magnifier; safe if unused.
trash "$share/knewstuff3" # Data for KNewStuff (downloadable content); important.
trash "$share/kplymouththemeinstaller" # Plymouth theme installer data; safe if unused.