# Determine the XDG cache directory (fallback to ~/.cache if not set)
set -q XDG_CACHE_HOME; and set -g FISH_CACHE_DIR "$XDG_CACHE_HOME/fish"; or set -g FISH_CACHE_DIR "$HOME/.cache/fish"
set -g LAST_DIR_FILE "$FISH_CACHE_DIR/last_dir"

# Save the current directory to a file every time it changes
function save_last_dir --on-variable PWD
    # Ensure the cache directory exists
    if not test -d "$FISH_CACHE_DIR"
        mkdir -p "$FISH_CACHE_DIR"
    end
    echo $PWD > "$LAST_DIR_FILE"
end

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
    set -gx EDITOR nvim
    set -gx PATH ".:$HOME/.bin:$cmddir/bin:$cmddir/firewall:$HOME/.lmstudio/bin:$PATH:$HOME/.local/bin:$JAVA_HOME/bin:$HOME/.cargo/bin:$HOME/Android/Sdk/platform-tools:$HOME/Android/Sdk/emulator"
    set -gx CPM_SOURCE_CACHE "$HOME/.cache/CPM"
    set -gx HISTSIZE 100000000
    set -gx SAVEHIST "$HISTSIZE"
    # set -gx XDG_DATA_DIRS "$HOME/.nix-profile/share:$XDG_DATA_DIRS"
    # set -gx LD_LIBRARY_PATH "." "$LD_LIBRARY_PATH"

    set -gx GPG_TTY "$(tty)"

    # improving build times: https://wiki.archlinux.org/title/Makepkg#Improving_build_times
    set -gx MAKEFLAGS "-j$(nproc)"
    set -gx LDFLAGS "-Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now -fuse-ld=mold"

    # Enabling Colors
    set -gx CMAKE_COLOR_DIAGNOSTICS ON
    set -gx CLICOLOR_FORCE 1
    set -gx NINJA_STATUS "\033[32m[%f/%t] \033[0m"
    set -gx FORCE_COLOR 1
    set -gx CARGO_TERM_COLOR always # Rust
    set -gx GL_COLOR always # Go
    set -gx GCC_COLORS 'error=01;31:warning=01;35:note=01;36:caret=01;32:locus=01:quote=01'
    set -gx NPM_CONFIG_COLOR always
    set -gx PY_COLORS 1 # Python
    set -gx RSPEC_COLOR 1 # Ruby
    set -gx BUILDKIT_COLORS 'run=green:info=cyan:warning=yellow:error=red' # Docker BuildKit
    set -gx GH_FORCE_TTY 100% # GitHub CLI
    set -gx TF_CLI_ARGS "-color" # Terraform



    # There are conflicts for "clang-format" for example
    # if [ -d /opt/depot_tools ];
    #   fish_add_path /opt/depot_tools
    # end

    # JAVA
    set -gx JAVA_HOME /usr/lib/jvm/java-20-openjdk
    if [ ! -d "$JAVA_HOME" ]
        set -gx JAVA_HOME /usr/lib/jvm/default
    end

    # VCPkg (pacman -S vcpkg):
    set -gx VCPKG_ROOT /opt/vcpkg
    if [ ! -d "$VCPKG_ROOT" ]
        set --erase VCPKG_ROOT
    end

    function conda -d 'lazy initialize conda'
        functions --erase conda
        eval /opt/miniconda3/bin/conda "shell.fish" hook | source
        # There's some opportunity to use `psub` but I don't really understand it.
        conda $argv
    end

    # Play a sound on indicating the status of the last command
    # if play_pipe_sound --is-possible;
    #     function precmd --on-event fish_postexec;
    #         set p_status "$pipestatus"
    #         echo -ne (set_color yellow)► [ "$p_status" ]
    #         setsid play_pipe_sound $p_status >/dev/null 2>&1 </dev/null &
    #     end
    # end

    source $HOME/.config/fish/aliases.fish
    source $HOME/.config/fish/functions.fish

    source $HOME/.config/fish/completions/completions.fish

    if [ -f "$HOME/.fishrc" ]
        source $HOME/.fishrc
    end

    # >>> conda initialize >>>
    # !! Contents within this block are managed by 'conda init' !!
    # if test -f /opt/miniconda3/bin/conda
    #     eval /opt/miniconda3/bin/conda "shell.fish" "hook" $argv | source
    # end
    # <<< conda initialize <<<

    source $HOME/.config/fish/cmd_timer.fish

    function _open_pdf
        echo "evince $argv"  # or your PDF viewer
    end
    function _open_editor
        echo "$EDITOR $argv"
    end
    function _open_editor
        echo "jless $argv"
    end
    abbr --add 'pdf' --regex '\S*\.pdf$' --function _open_pdf
    abbr --add 'nvim-text-files' --regex '\S*\.(md|cpp|hpp|cxx|ixx|h|txt)$' --function _open_editor
    abbr --add 'json' --regex '\S*\.(json|yml|yaml)$' --function _open_editor

    abbr -a "c." --position anywhere --set-cursor "c.p | % | c.c" # Paste, Modify, Copy
    abbr -a "issue" --set-cursor "gh issue view --comments % | clean.privacy | c.c"
    abbr for-dirs --set-cursor=! "$(string join \n -- 'for dir in */' 'cd $dir' '!' 'cd ..' 'end')"
    abbr -a pro prompt

    abbr -a "yt.save" "c.p | yt.links >> ~/YouTube/tmp/links.txt; exit"
    abbr -a "yt.dl" "c.p | yt.links | xargs download --cookies-from-browser firefox 1440p; exit"
    abbr -a hx helix



    # On startup, read the file and cd into it
    # Only cd if we are starting in the home directory
    if test "$PWD" = "$HOME"
        if test -f "$LAST_DIR_FILE"
            set -l last_dir (cat "$LAST_DIR_FILE")
            if test -d "$last_dir"
                builtin cd "$last_dir"
            end
        end
    end

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
