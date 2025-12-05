#!/bin/bash

trash "$share/gnome-box"
trash "$share/gnome-photos"
trash "$share/gnome-maps"
trash "$share/gnome-remote-desktop"
trash "$share/grilo-plugins" # Plugins for Grilo media discovery (GNOME); safe if not using related apps.

trash "$share/gthumb" # Data for gThumb image viewer; safe to remove if unused.
trash "$share/yelp" # Data for Yelp help viewer (GNOME); safe if unused.