#!/bin/bash

# export DBUS_SESSION_BUS_ADDRESS environment variable useful when the script is set as a cron job
PID="$(pgrep -x gnome-session | head -n 1)"
if [[ -n "$PID" ]]; then
    export DBUS_SESSION_BUS_ADDRESS="$(grep -z DBUS_SESSION_BUS_ADDRESS "/proc/$PID/environ" | cut -f2- -d= | tr -d '\0')"
fi

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
    cat <<EOF
Usage: $(basename "$0") <background.jpg>

  Write the current task list on <background.jpg> and set it as
  the GNOME desktop wallpaper.

Options:
  -h, --help          Show this help message
EOF
    exit 0
fi

background="$1"
if [[ ! -f "$background" ]]; then
    echo "Usage: $(basename "$0") background.jpg" >&2
    exit 1
fi

old="$(gsettings get org.gnome.desktop.background picture-uri | tr -d "'")"
url="$(mktemp --suffix=.jpg)"
text="$(task list limit:40)"

printf 'Writing tasks to %s' "$background"
convert "$background" -font FreeMono -fill white -pointsize 20 -gravity east -annotate +100+0 "$text" "${url}"
printf '\r\033[KSaved in %s\n' "$url"
echo
echo "Old background: $old"
echo "New background: $url"

gsettings set org.gnome.desktop.background picture-uri "'file://${url}'"
gsettings set org.gnome.desktop.background picture-uri-dark "'file://${url}'"

if [[ "$old" == "file:///tmp/"* ]]; then
    rm -f "${old#file://}"
fi

