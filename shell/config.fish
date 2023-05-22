

# Disable fish greeting
set -g fish_greeting

set -g cmddir "$HOME/cmd"

#export TERM=alacritty
# export TERM=kitty
export EDITOR="nvim"
export PATH=".:$HOME/.bin:$cmddir/bin:$cmddir/firewall:$PATH:$JAVA_HOME/bin"
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"
export HISTSIZE="100000000"
export SAVEHIST="$HISTSIZE"

# JAVA
export JAVA_HOME="/usr/lib/jvm/java-19-openjdk"
if [ ! -d "$JAVA_HOME" ]
    export JAVA_HOME=/usr/lib/jvm/default
end


function conda -d 'lazy initialize conda'
  functions --erase conda
  eval /opt/miniconda3/bin/conda "shell.fish" "hook" | source
  # There's some opportunity to use `psub` but I don't really understand it.
  conda $argv
end


if status is-interactive
    # Commands to run in interactive sessions can go here

    # Play a sound on indicating the status of the last command
    if play_pipe_sound --is-possible;
        function precmd --on-event fish_postexec;
            play_pipe_sound $pipestatus &
        end
    end


    source $HOME/.config/fish/aliases.fish;
    source $HOME/.config/fish/completions/completions.fish;

    # >>> conda initialize >>>
    # !! Contents within this block are managed by 'conda init' !!
    # if test -f /opt/miniconda3/bin/conda
    #     eval /opt/miniconda3/bin/conda "shell.fish" "hook" $argv | source
    # end
    # <<< conda initialize <<<
end




function try -d "try a command until it works."
    while ! $argv
        sleep 1s; # So we can cancel
    end
end

# Usage: trynot isup facebook.com && nordvpn connect
function trynot -d "Try a command until it doesn't work"
    while $argv
        sleep 1s; # So we can cancel
    end
end

