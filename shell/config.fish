
if status is-interactive
    # Commands to run in interactive sessions can go here

    # https://github.com/fish-shell/fish-shell/wiki/Bash-Style-Command-Substitution-and-Chaining-(!!-!$)
    # function fish_user_key_bindings
    #     fish_hybrid_key_bindings
    #     bind -M insert ! bind_bang
    #     bind -M insert '$' bind_dollar
    # end

    # Disable fish greeting
    set -gx fish_greeting

    set -gx cmddir "$HOME/cmd"

    # set -gx TERM=alacritty
    # set -gx TERM=kitty
    set -gx EDITOR "nvim"
    set -gx PATH ".:$HOME/.bin:$cmddir/bin:$cmddir/firewall:$PATH:$HOME/.local/bin:$JAVA_HOME/bin:$HOME/.cargo/bin:$HOME/Android/Sdk/platform-tools:$HOME/Android/Sdk/emulator"
    set -gx XDG_DATA_DIRS "$HOME/.nix-profile/share:$XDG_DATA_DIRS"
    set -gx CPM_SOURCE_CACHE "$HOME/.cache/CPM"
    set -gx HISTSIZE "100000000"
    set -gx SAVEHIST "$HISTSIZE"
    set -gx LD_LIBRARY_PATH "." $LD_LIBRARY_PATH

    set -gx GPG_TTY "$(tty)"

    # improving build times: https://wiki.archlinux.org/title/Makepkg#Improving_build_times
    set -gx MAKEFLAGS "-j$(nproc)"
    set -gx LDFLAGS "-Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now -fuse-ld=mold"

    # There are conflicts for "clang-format" for example
    # if [ -d /opt/depot_tools ];
    #   fish_add_path /opt/depot_tools
    # end

    # JAVA
    set -gx JAVA_HOME "/usr/lib/jvm/java-20-openjdk"
    if [ ! -d "$JAVA_HOME" ]
        set -gx JAVA_HOME /usr/lib/jvm/default
    end

    # VCPkg (pacman -S vcpkg):
    set -gx VCPKG_ROOT "/opt/vcpkg"
    if [ ! -d "$VCPKG_ROOT" ]
        set --erase VCPKG_ROOT
    end

    function conda -d 'lazy initialize conda'
      functions --erase conda
      eval /opt/miniconda3/bin/conda "shell.fish" "hook" | source
      # There's some opportunity to use `psub` but I don't really understand it.
      conda $argv
    end

    # Play a sound on indicating the status of the last command
    if play_pipe_sound --is-possible;
        function precmd --on-event fish_postexec;
            set p_status "$pipestatus"
            echo -ne (set_color yellow)► [ "$p_status" ]
            setsid play_pipe_sound $p_status >/dev/null 2>&1 </dev/null &
        end
    end


    source $HOME/.config/fish/aliases.fish;
    source $HOME/.config/fish/completions/completions.fish;

    if [ -f "$HOME/.fishrc" ];
        source $HOME/.fishrc;
    end

    # >>> conda initialize >>>
    # !! Contents within this block are managed by 'conda init' !!
    # if test -f /opt/miniconda3/bin/conda
    #     eval /opt/miniconda3/bin/conda "shell.fish" "hook" $argv | source
    # end
    # <<< conda initialize <<<


    source $HOME/.config/fish/cmd_timer.fish
end




# function try -d "try a command until it works."
#     while ! $argv
#         sleep 1s; # So we can cancel
#     end
# end
# 
# # Usage: trynot isup facebook.com && nordvpn connect
# function trynot -d "Try a command until it doesn't work"
#     while $argv
#         sleep 1s; # So we can cancel
#     end
# end

