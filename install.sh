#!/usr/bin/env bash


dir=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
configs_dir="$dir/configs";

should_uninstall=false # boolean

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

    if $should_uninstall; then
	    log "Uninstalling $install_path"
	    rm -rf "$install_path";
	    return;
    fi

    cmd_inode=$(stat -c %i -- "$cmd_path");
    install_inode=$(stat -c %i -- "$install_path" 2>/dev/null);
    if [ "$cmd_inode" = "$install_inode" ] && [ ! -z "$install_inode" ]; then
        log "Already Linked: $cmd_inode $install_inode $cmd_path -> $install_path";
        log
        return;
    fi;
    mkdir -p "$(dirname $install_path)";
    if $forced; then
        if ln -f "$cmd_path" "$install_path"; then
            if [ -z "$install_node" ]; then
                log "Linked: $cmd_path -> $install_path";
            else
                log "Replaced with link: $cmd_path -> $install_path";
            fi;
        else
            error "Link Failed: $cmd_path -> $install_path";
            log "We're gonna try to copy the file."
            if cp "$cmd_path" "$install_path"; then
                log "Copied $cmd_path -> $install_path";
            else
                error "Copying failed too for $install_path."
            fi
        fi;
    else
        if ln "$cmd_path" "$install_path"; then
            log "Linked: $cmd_path -> $install_path";
        else
            error "Link Failed: $cmd_path -> $install_path";
            log "We're gonna try to copy the file."
            if cp "$cmd_path" "$install_path"; then
                log "Copied $cmd_path -> $install_path";
            else
                error "Copying failed too for $install_path."
            fi
        fi;
    fi;
    log
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


# chromium config files
function chromium {
    if command -v chromium &>/dev/null; then
        cmd_config="$configs_dir/chromium-flags.conf";
        chromium_config="$HOME/.config/chromium-flags.conf";
        install "$cmd_config" "$chromium_config";
    else
        warning "Chromium is not installed.";
    fi
}

# SpaceVim
function spacevim {
    if (command -v vim &>/dev/null || command -v nvim &>/dev/null) &&
        [ -d "$HOME/.SpaceVim" ]; then
        cmd_config="$configs_dir/SpaceVim.d/init.toml";
        sp_config="$HOME/.SpaceVim.d/init.toml";
        install "$cmd_config" "$sp_config";
    else
        warning "Either vim/nvim or SpaceVim is not installed.";
    fi
}

# Firefox Policies
function firefox-policies {
    # https://mozilla.github.io/policy-templates/
    cmd_config="$configs_dir/firefox/policies.json";
    sp_config="/etc/firefox/policies/policies.json";
    install "$cmd_config" "$sp_config";
}



# TV Shortcuts
function tv_shortcuts {
    if command -v node &>/dev/null; then
        # icons_dir="$HOME/.icons/tv-icons";
        builtin cd "$dir/tv";
        rm -rf tmp;
        mkdir -p tmp;
        node generate-telewebion.js

        log
        for img in *.svg; do
            name=$(basename "$img");
            name="${name/.svg/}";
            log -ne "\r\033[KInstalling icon $name";
            for size in 16 24 32 48 64 96 128 192 256 512; do
                convert -background transparent -resize $size -extent ${size}x${size} -gravity center "$img" "tmp/$name-${size}.png"
                xdg-icon-resource install --novendor --size $size --context apps "tmp/$name-${size}.png" "tv.$name"
            done
            cp -f "$img" "$HOME/.local/share/icons/hicolor/scalable/apps/tv.$name.svg"
        done

        cp icon-*.png tmp/.
        for img in tmp/icon-*.png; do
            name=$(basename "$img");
            name="${name/.png/}";
            name="${name/icon-/}";
            log -ne "\r\033[KInstalling icon $name";
            # convert -background transparent "$img" -define icon:auto-resize=16,24,32,48,64,128 "${img/.png/.ico}"
            for size in 16 24 32 48 64 96 128 192 256 512; do
                convert -background transparent -resize $size -extent ${size}x${size} -gravity center "$img" "tmp/$name-${size}.png"
                xdg-icon-resource install --novendor --size $size --context apps "tmp/$name-${size}.png" "tv.$name"
            done
        done
        log -ne "\r";
        cp *.desktop tmp/.
        for file in tmp/*.desktop; do
            desktop-file-install --dir=$HOME/.local/share/applications "$file"
        done
        update-desktop-database $HOME/.local/share/applications
        rm -f tmp/*.desktop
        rm -rf tmp
        builtin cd "$dir";
    else
        log "Can't intall TV desktop files. Nodejs/imagemagic(convert) is not installed.";
    fi
}

function codeshell_shortcut {
    export ProjectRoot="$dir"
    "$dir/bin/bashify" "$dir/applications/codeshell.desktop" > "$HOME/.local/share/applications/codeshell.desktop";
    log "Installed Codeshell Desktop Shortcut.";
    update-desktop-database $HOME/.local/share/applications
}

function setup_fish {
    fish_dir="$HOME/.config/fish"
    shell_dir="$dir/shell"
    install "$shell_dir/aliases.fish" "$fish_dir/aliases.fish"
    install "$shell_dir/config.fish" "$fish_dir/config.fish"
    install "$shell_dir/completions.fish" "$fish_dir/completions/completions.fish"
    install "$dir/assets/ok.oga" "$fish_dir/assets/ok.oga"
    install "$dir/assets/error.oga" "$fish_dir/assets/error.oga"
}

# default values;
forced=false
for i in "$@"; do
    case $i in
        -f|--force)
            forced=true;
            shift;
            ;;

        -h|--help|help)
            log "install.sh [--forced]"
            log "install.sh --help"
            log "install.sh fish"
            log "install.sh spacevim"
            log "install.sh tv"
            log "install.sh chromium"
            log "install.sh codeshell"
            log "install.sh --all"
            log "install.sh uninstall fish"
            log "install.sh uninstall all"
            shift;
            ;;

        chrome|chromium)
            chromium;
            shift;
            ;;

        spacevim|vim)
            spacevim;
            shift;
            ;;

        tv|television|tv-shortcuts)
            tv_shortcuts;
            shift;
            ;;

        codeshell)
            codeshell_shortcut;
            shift;
            ;;

        firefox-policies|firefox)
            firefox-policies;
            shift;
            ;;

        fish)
            setup_fish;
            shift;
            ;;

        uninstall|--uninstall|remove|rm|--remove|--rm)
	    should_uninstall=true
            shift;
            ;;

        all|--all|-a)
            setup_fish;
            chromium;
            spacevim;
            codeshell_shortcut;
            tv_shortcuts;
	    firefox-policies;
	    setup_fish;
            exit;
            ;;

        *)
            log "Unknown flags/application.";
            exit;
            ;;
    esac
done

