#!/bin/bash

share="${share:-$HOME/.local/share}"

if [[ ! -d "$share" ]]; then
    echo "Error: share directory not found: $share" >&2
    exit 1
fi

# GUI Apps
for app in ClipGrab ark app.hiddify.com ghostwriter drkonqi okular \
           org.gnome.TextEditor arianna artikulate audacity \
           com.yubico.yubioath dragonplayer org.gnome.SoundRecorder \
           shotwell skanpage; do
    [[ -d "$share/$app" ]] && trash "$share/$app"
done

# Terminal apps
for app in ranger NuGet man crush imhex; do
    [[ -d "$share/$app" ]] && trash "$share/$app"
done

# Others
for app in gvfs-metadata RefSrcSymbols Symbols SymbolSourceSymbols; do
    [[ -d "$share/$app" ]] && trash "$share/$app"
done