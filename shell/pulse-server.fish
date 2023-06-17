#!/usr/bin/fish

# This function will make sure the pulseaudio's audio will be played in the
# specified remote server.
# Pass "auto" to use the IP address specified in SSH_CLIENT
function export_pulse_server -d "Set PULSE_SERVER"
    set uname "$argv[1]";
    set fallback_ip "$argv[2]";
    
    if [ "$uname" = "auto" ] && [ ! -z "$SSH_CLIENT" ];
	set uname_ip (string split -f 1 " " "$SSH_CLIENT");
    else
	set uname_ip (dig +short +timeout=1 "$uname" | grep '^[.0-9]*$' | head -n 1);
    end

    if [ ! -z "$uname_ip" ];
        set -gx PULSE_SERVER tcp:$uname_ip:4713
    else if ping -c 1 -q -w 1 -W 1 $fallback_ip >/dev/null;
        set -gx PULSE_SERVER tcp:$fallback_ip:4713;
    end
end

