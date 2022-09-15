#!/usr/bin/env bash


dir=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

function log {
    echo $@;
}

function warning {
    echo $@;
}

function error {
    echo $@;
}

# default values;
forced=false
for i in "$@"; do
    case $i in
        -f|--force)
            forced=true;
            shift;
            ;;

        -h|--help)
            echo "install.sh [--forced]"
            echo "install.sh --helped"
            shift;
            ;;

        *)
            echo "Unknown flags.";
            exit;
            ;;
    esac
done


# chromium config files
if command -v chromium &>/dev/null; then
    cmd_config="$dir/configs/chromium-flags.conf";
    chromium_config="$HOME/.config/chromium-flags.conf";
    if [ -f "$chromium_config" ]; then
        if $forced; then
            if ln -f "$cmd_config" "$chromium_config"; then
                log "chromium-flags.conf linked from $dir/configs/chromium-flags.conf to $HOME/.config/chromium-flags.conf"
            else
                error "Cannot link chromium flags file.";
            fi;
        else
            error "$chromium_config file already exists. Use --force to replace it anyway.";
        fi;
    else
        if ln "$cmd_config" "$chromium_config"; then
            log "chromium-flags.conf linked from $dir/configs/chromium-flags.conf to $HOME/.config/chromium-flags.conf"
        else
            error "Cannot link chromium flags file.";
        fi;
    fi
else
    warning "Chromium not installed.";
fi
