#!/usr/bin/fish

function detach
    if test (count $argv) -eq 0
        echo "Usage: detach <command> [args...]"
        echo "Detach the process and exit the current shell"
        return 1
    end

    # Fully detach: new session, drop I/O, run in background
    setsid $argv[1] $argv[2..-1] </dev/null >/dev/null 2>&1 &

    # Exit the *current* shell
    exit 0
end
