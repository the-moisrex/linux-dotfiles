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




# TV Shortcuts
if command -v node 2>/dev/null; then
    # icons_dir="$HOME/.icons/tv-icons";
    builtin cd "$dir/tv";
    node generate.js
    # mkdir -p "$icons_dir";

    echo
    for img in *.png; do
        name="${img/.png/}"
        name="${name/icon-/}"
        echo -ne "\r\033[KInstalling icon $name";
        # convert -background transparent "$img" -define icon:auto-resize=16,24,32,48,64,128 "${img/.png/.ico}"
        convert -background transparent "$img" -resize 128x128 "$name.png"
        xdg-icon-resource install --size 128 --context apps "$name.png" "irtv-$name"
        convert -background transparent "$img" -resize 64x64 "$name.png"
        xdg-icon-resource install --size 64 --context apps "$name.png" "irtv-$name"
        convert -background transparent "$img" -resize 32x32 "$name.png"
        xdg-icon-resource install --size 32 --context apps "$name.png" "irtv-$name"
        convert -background transparent "$img" -resize 16x16 "$name.png"
        xdg-icon-resource install --size 16 --context apps "$name.png" "irtv-$name"
    done
    echo -ne "\r";
    rm -f *.png;
    for file in $dir/tv/*.desktop; do
        desktop-file-install --dir=$HOME/.local/share/applications "$file"
    done
    update-desktop-database $HOME/.local/share/applications
    rm *.desktop
    builtin cd "$dir";
else
    echo "Can't intall TV desktop files. Nodejs is not installed.";
fi
