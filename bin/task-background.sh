#!/bin/bash

# export DBUS_SESSION_BUS_ADDRESS environment variable useful when the script is set as a cron job
PID=$(pgrep gnome-session | head -n 1)
export DBUS_SESSION_BUS_ADDRESS=$(grep -z DBUS_SESSION_BUS_ADDRESS /proc/$PID/environ | cut -f2- -d= | tr -d '\0')


background="$1";
if [ ! -f "$background" ]; then
    echo "Usage: $0 background.jpg";
    exit;
fi;


#secs=$($(($(date +%s) - $(date +%s -r "$old"))));

#if [ "$(task ids)" != "$(cat /tmp/tasks 2>/dev/null)" ]; then
  
  old=$(gsettings get org.gnome.desktop.background picture-uri | tr -d "'")
  url=$(mktemp --suffix=.jpg)
  text=$(task list limit:40)

  echo -en "Writing tasks to $background";
  convert "$background" -font FreeMono -fill white -pointsize 20 -gravity east -annotate +100+0 "$text" "${url}";
  echo -en "\r\033[KSaved in $url"
  echo
  echo
  echo Old background: $old
  echo New background: $url

  gsettings set org.gnome.desktop.background picture-uri "'file://${url}'";
  gsettings set org.gnome.desktop.background picture-uri-dark "'file://${url}'";

  if [[ "$old" == "file:///tmp/"* ]]; then
    rm -f ${old/file:\/\//};
  fi;

  #task ids > /tmp/tasks;

#fi;
exit;

