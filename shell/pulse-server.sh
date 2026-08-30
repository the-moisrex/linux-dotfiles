#!/bin/bash

# This function will make sure the pulseaudio's audio will be played in the
# specified remote server.
#
# Usage: export_pulse_server <hostname> <fallback_ip>
#
# Arguments:
#   hostname     The hostname to resolve for the PulseAudio server
#   fallback_ip  Fallback IP address if hostname resolution fails
#
# This function attempts to resolve the hostname to an IP address. If successful,
# it sets PULSE_SERVER to tcp:<resolved_ip>:4713. If resolution fails, it tries
# the fallback IP address.

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
