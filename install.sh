#!/usr/bin/env bash


dir=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
configs_dir="$dir/configs";

function log {
    echo $@;
}

function warning {
    echo $@;
}

function error {
    echo $@;
}

function link_item {
    cmd_path="$1";
    install_path="$2";
    cmd_inode=$(stat -c %i -- "$cmd_path");
    install_inode=$(stat -c %i -- "$install_path" 2>/dev/null);
    if [ "$cmd_inode" = "$install_inode" ] && [ ! -z "$install_inode" ]; then
        log "Already Linked: $install_inode $cmd_path -> $install_path";
        return;
    fi;
    if $forced; then
        if ln -f "$cmd_path" "$install_path"; then
            if [ -z "$install_node" ]; then
                log "Linked: $cmd_path -> $install_path";
            else
                log "Replaced with link: $cmd_path -> $install_path";
            fi;
        else
            error "Link Failed: $cmd_path -> $install_path";
        fi;
    else
        if ln "$cmd_path" "$install_path"; then
            log "Linked: $cmd_path -> $install_path";
        else
            error "Link Failed: $cmd_path -> $install_path";
        fi;
    fi;
}

function install {
    cmd_path="$1";
    install_path="$2";
    if [ -f "$cmd_path" ]; then
        link_item "$cmd_path" "$install_path";
    elif [ -d "$cmd_path" ]; then
        link_item "$cmd_path" "$install_path";
    else
        error "The $cmd_path is not a file/dir; can't install it.";
    fi;
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
    cmd_config="$configs_dir/chromium-flags.conf";
    chromium_config="$HOME/.config/chromium-flags.conf";
    install "$cmd_config" "$chromium_config";
else
    warning "Chromium is not installed.";
fi


# SpaceVim
if (command -v vim &>/dev/null || command -v nvim &>/dev/null) &&
    [ -d "$HOME/.SpaceVim" ]; then
    cmd_config="$configs_dir/SpaceVim.d/init.toml";
    sp_config="$HOME/.SpaceVim.d/init.toml";
    install "$cmd_config" "$sp_config";
else
    warning "Either vim/nvim or SpaceVim is not installed.";
fi
