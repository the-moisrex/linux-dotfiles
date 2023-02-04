#!/bin/bash

# This function will make sure the pulseaudio's audio will be played in the
# specified remote server.
function export_pulse_server {
    uname="$1";
    fallback_ip="$2";

    uname_ip=$(dig +short +timeout=1 $uname | grep '^[.0-9]*$' | head -n 1);
    if [ ! -z "$uname_ip" ]; then
        export PULSE_SERVER=tcp:$uname_ip:4713
    elif ping -c 1 -q -w 1 -W 1 $fallback_ip >/dev/null; then
        export PULSE_SERVER=tcp:$fallback_ip:4713;
    fi
}


