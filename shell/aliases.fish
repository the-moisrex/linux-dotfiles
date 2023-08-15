
# Navigation
function ..    ; cd .. ; end
function ...   ; cd ../.. ; end
function ....  ; cd ../../.. ; end
function ..... ; cd ../../../.. ; end
function cd    ; builtin cd $argv; ls; end

# Utilities
function grep     ; command grep --color=auto $argv ; end

# typos and abbreviations
abbr g git
abbr gi git
abbr gti git
abbr yearn yarn
abbr v vim
abbr bwre brew
abbr brwe brew

if command -v exa >/dev/null
    set ls_exec "exa --icons"
    set ls_cmd "$ls_exec --group-directories-first"

    # some more ls aliases
    alias ll="$ls_cmd -alFh"
    alias la="$ls_cmd -a"
    alias l="$ls_cmd -F"
    alias lh="$ls_cmd -lh"
else if command -v lsd >/dev/null
    set ls_exec "lsd"
    set ls_cmd "$ls_exec --group-directories-first"

    # some more ls aliases
    alias ll="$ls_cmd -alFh"
    alias la="$ls_cmd -a"
    alias l="$ls_cmd -F"
    alias lh="$ls_cmd -lh"
else
    set ls_exec "ls"
    set ls_cmd "$ls_exec --hyperlink=auto"

    # some more ls aliases
    alias ll="$ls_cmd -alFh"
    alias la="$ls_cmd -A"
    alias l="$ls_cmd -CF"
    alias lh="$ls_cmd -lh"
end

# enable color support of ls and also add handy aliases
if [ -x /usr/bin/dircolors ]
    # test -r ~/.dircolors && eval (dircolors -c ~/.dircolors) || eval (dircolors -c)
    if [ "$ls_exec" = "ls" ]
        alias ls="$ls_exec --color=auto --hyperlink=auto"
    end
    alias dir="dir --color=auto"
    alias vdir="vdir --color=auto"
    alias grep="grep --color=auto"
    alias fgrep="fgrep --color=auto"
    alias egrep="egrep --color=auto"
end

alias ls="$ls_cmd"

# Network Start, Stop, and Restart

#alias light='xbacklight -set'


# Apt
#alias update='sudo apt -y update'
#alias upgrade='sudo apt-get -y update && sudo apt-get -y --allow-unauthenticated upgrade && sudo apt-get autoclean && sudo apt-get autoremove && exit 0'
#alias search='sudo apt search'
#alias links='links2'
# Install and Remove Packages
#alias install='sudo apt-get -y install'
#alias uninstall='sudo apt-get --purge autoremove '
#alias search-installed='sudo dpkg --get-selections '
#alias upgrade-pips='sudo pip freeze --local | grep -v '^\-e' | cut -d = -f 1  | xargs -n1 sudo pip install -U
#alias cleanPC='sudo apt-get -y autoclean && sudo apt-get -y clean && sudo apt-get -y autoremove'


alias lsdir="$ls_exec -ld */"
#alias display='eog -w'
#alias emptyDir='find . -empty -type d -delete'
#alias meng='cd ${HOME}/Dropbox/MEng_Stuff/MEng-Progress'
#alias media='sshfs -o reconnect media@192.168.1.10:/mnt /home/"${USER}"/mnt/media_srv'
#alias reboot='sudo shutdown -r now'
#alias shutdown='sudo shutdown -h now'
alias paux="ps aux | grep"
# alias cd.="cd .."
# alias ..="cd .."
# alias ...="cd ../../../"
# alias ....="cd ../../../../"
# alias .....="cd ../../../../"
# alias .4="cd ../../../../"
# alias .5="cd ../../../../.."
# alias cd..="cd .."

alias yt="try yt-dlp --restrict-filenames --continue --embed-thumbnail --embed-subs --embed-metadata --embed-chapters --sub-langs all,-live_chat"
alias yt-list="yt --output '%(playlist_index)s - %(title)s [%(id)s].%(ext)s'"
alias yt-audio="yt -x"
alias yt-audio-list="yt-list -x"
alias youtube="yt"
alias yt8000="yt --proxy=http://localhost:8000"
alias yt8090="yt --proxy=http://localhost:8090"


# Useful Alias
# Add an "alert" alias for long running commands.  Use like so:
# alias alert='notify-send --urgency=low -i "$([ $? = 0 ] && echo terminal || echo error)" "$(history|tail -n1|sed -e '\''s/^\s*[0-9]\+\s*//;s/[;&|]\s*alert$//'\'')"'

alias killfirefox="pkill -9 firefox"
alias killslack="pkill -9 slack"
alias CD="cd"
alias Cd="cd"

set no_proxy "localhost, 127.0.0.1/8, 192.168.0.0/16, 10.0.0.0/8, *.localhost"

alias prox1089="https_proxy=socks5://127.0.0.1:1089 http_proxy=socks5://127.0.0.1:1089 no_proxy='$no_proxy'"
alias prox9550="https_proxy=socks5://127.0.0.1:9550 http_proxy=socks5://127.0.0.1:9550 no_proxy='$no_proxy'"
alias prox9050="https_proxy=socks5://127.0.0.1:9050 http_proxy=socks5://127.0.0.1:9050 no_proxy='$no_proxy'"
alias prox9150="https_proxy=socks5://127.0.0.1:9150 http_proxy=socks5://127.0.0.1:9050 no_proxy='$no_proxy'"
alias prox8090="http_proxy=http://127.0.0.1:8090 https_proxy=http://127.0.0.1:8090 no_proxy='$no_proxy'"
alias prox8000="http_proxy=http://127.0.0.1:8000 https_proxy=http://127.0.0.1:8000 no_proxy='$no_proxy'"
alias prox8080="http_proxy=http://127.0.0.1:8080 https_proxy=http://127.0.0.1:8080 no_proxy='$no_proxy'"
alias prox8118="http_proxy=http://localhost:8118 https_proxy=http://localhost:8118 no_proxy='$no_proxy'"
alias tmp="cd $(mktemp -d)"
alias open="emacsclient -nc -s initd -a nvim"
alias edit="emacsclient -nw -s initd -a nvim"
alias svim="vim --clean" # Simple vim
alias snvim="nvim --noplugin --clean" # Simple nvim
alias proxychains="http_proxy=\"\" https_proxy=\"\" all_proxy=\"\" proxychains"
alias c="xclip -selection clipboard"


# git aliases
alias ggrep="git grep --heading --break -n";
alias task-tui="taskwarrior-tui"
alias tasktui="taskwarrior-tui"


alias ctl="sudo systemctl"
# alias status="sudo systemctl status"
alias restart="sudo systemctl restart"


alias ip="ip -c"

if command -v firefox-developer-edition >/dev/null
    if ! command -v firefox >/dev/null
        alias firefox="firefox-developer-edition"
    end
end


# DBUS

alias clipboard.paste="qdbus org.kde.klipper /klipper org.kde.klipper.klipper.getClipboardContents"
alias clipboard.copy="xargs -0 qdbus org.kde.klipper /klipper org.kde.klipper.klipper.setClipboardContents {}"
alias clipboard.history="qdbus org.kde.klipper /klipper org.kde.klipper.klipper.getClipboardHistoryMenu"

alias cb.paste="clipboard.paste"
alias cb.cp="clipboard.copy"
alias cb.hist="clipboard.history"


alias ping="ping -DO" #  timeout
alias please="sudo"


function unsudo -d "un-sudo some commands"
    for arg in $argv
        if command -v $arg >/dev/null
            alias $arg="sudo $arg"
        end
    end
end

unsudo pacman systemctl ausearch reboot poweroff auditctl aureport dnstop nftop nft

