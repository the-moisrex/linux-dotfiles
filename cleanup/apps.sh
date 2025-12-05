#!/bin/bash

# GUI Apps
trash "$share/ClipGrab"
trash "$share/ark"
trash "$share/app.hiddify.com"
trash "$share/ghostwriter"
trash "$share/drkonqi"
trash "$share/okular"
trash "$share/org.gnome.TextEditor"
trash "$share/arianna"
trash "$share/artikulate"
trash "$share/audacity"
trash "$share/com.yubico.yubioath"
trash "$share/dragonplayer"
trash "$share/org.gnome.SoundRecorder" # Data for GNOME Sound Recorder; safe if unused.
trash "$share/shotwell" # Data for Shotwell photo manager; safe if unused, loses library.
trash "$share/skanpage" # Data for Skanpage scanner; safe if unused.

# Terminal apps
trash "$share/ranger"
trash "$share/NuGet"
trash "$share/man" # User Man Pages
trash "$share/crush"
trash "$share/imhex" # Data for ImHex hex editor; safe to remove if unused.

# trash "$share/cryfs"

# Others
trash "$share/gvfs-metadata" # File system metadata cache; safe to remove, regenerates automatically.
# trash "$share/IsolatedStorage" # Isolated storage for .NET/Mono apps; safe if no related apps.
trash "$share/RefSrcSymbols" # Likely reference source symbols for debugging; safe to remove if not developing.
trash "$share/Symbols" # Likely debug symbols; safe to remove.
trash "$share/SymbolSourceSymbols" # Similar to above, debug symbols; safe.