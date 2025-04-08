# ~/.config/nushell/config.nu
# (Add these lines to your existing config.nu, likely after sourcing env.nu)

# --- Git Root Calculation ---
# Finds the root directory of the current git repository
# Returns the path as a string, or null if not in a git repo or error occurs
def calc_git_root [] {
    mut current_path = Path.pwd()
    loop {
        let git_dir = ($current_path | path join ".git")
        if (($git_dir | path type) == "dir") {
            return $current_path
        }
        # Check if we've reached the root
        let parent_path = ($current_path | path dirname)
        if $parent_path == $current_path {
            # Reached filesystem root without finding .git
            return null
        }
        $current_path = $parent_path
    }
}

# --- Navigation ---

# Change directory relative to the git root
def cdi [path_in_repo?: string = ""] {
    let root = calc_git_root
    match ($root) {
        _ => {
            let target_path = ([$root $path_in_repo] | path join)
            cd $target_path
        }
        $none => {
            error make { msg: "Not inside a git repository." }
        }
    }
}


# Change directory relative to the projects root
def proj [path_in_proj?: string = ""] {
    if ($env.projects_root? | is-empty) {
        error make { msg: "$env.projects_root is not set."}
        return
    }
    let target_path = ([$env.projects_root $path_in_proj] | path join)
    cd $target_path
}
alias cdproj = proj


# --- Utilities ---
alias grep = grep --color=auto
alias fgrep = fgrep --color=auto # Fish had these in the dircolors check
alias egrep = egrep --color=auto # Fish had these in the dircolors check


# --- Typos and Abbreviations ---
alias g = git
alias gi = git
alias gti = git
alias yearn = yarn
alias v = vim
alias n = nvim
alias bwre = brew
alias brwe = brew
alias tg = telegram.links
alias tg.link = telegram.links


# Define common ls aliases using the determined command
# Note: Nushell aliases don't directly expand other aliases in their definition in the same way
#       as Fish/Bash sometimes do. We parse the base command string here.
#       Alternatively, define a helper function `def run-ls [...] { $ls_command ...$rest }`
alias ll = ls -al
alias la = ls -a
alias l = ls
alias dir = dir --color=auto   # Assumes 'dir' is GNU dir
alias vdir = vdir --color=auto # Assumes 'vdir' is GNU vdir


# --- yt-dlp Aliases ---
alias dl = download

let yt_base_opts = "--restrict-filenames --continue --embed-thumbnail --write-auto-sub --embed-subs --embed-metadata --embed-chapters --sub-langs en*,es,fa,-live_chat"
alias yt = yt-dlp $yt_base_opts
alias yt-list = yt --output '%(playlist_index)s - %(title)s [%(id)s].%(ext)s'
alias yt-audio = yt -x
alias yt-1440 = yt -f 'bestvideo[ext=mp4][width<3000][height<=1600]+bestaudio[ext=m4a]/bestvideo[ext=webm][width<3000][height<=1600]+bestaudio[ext=webm]/bestvideo[width<3000][height<=1600]+bestaudio/best[width<3000][height<=1600]/best'
alias yt-1080 = yt -f 'bestvideo[ext=mp4][width<2000][height<=1200]+bestaudio[ext=m4a]/bestvideo[ext=webm][width<2000][height<=1200]+bestaudio[ext=webm]/bestvideo[width<2000][height<=1200]+bestaudio/best[width<2000][height<=1200]/best'
alias yt-720 = yt -f 'bestvideo[ext=mp4][width<1500][height<=720]+bestaudio[ext=m4a]/bestvideo[ext=webm][width<1500][height<=720]+bestaudio[ext=webm]/bestvideo[width<1500][height<=720]+bestaudio/best[width<1500][height<=720]/best'
alias yt-480 = yt -f 'bestvideo[ext=mp4][width<=900][height<=480]+bestaudio[ext=m4a]/bestvideo[ext=webm][width<=900][height<=480]+bestaudio[ext=webm]/bestvideo[width<=900][height<=480]+bestaudio/best[width<=900][height<=480]/best'
alias yt-360 = yt -f 'bestvideo[ext=mp4][width<=700][height<=360]+bestaudio[ext=m4a]/bestvideo[ext=webm][width<=700][height<=360]+bestaudio[ext=webm]/bestvideo[width<=700][height<=360]+bestaudio/best[width<=700][height<=360]/best'
alias yt-240 = yt -f 'bestvideo[ext=mp4][width<=500][height<=240]+bestaudio[ext=m4a]/bestvideo[ext=webm][width<=500][height<=240]+bestaudio[ext=webm]/bestvideo[width<=500][height<=240]+bestaudio/best[width<=500][height<=240]/best'
alias yt-audio-list = yt-list -x
alias youtube = yt
alias yt8000 = yt --proxy=http://localhost:8000
alias yt8090 = yt --proxy=http://localhost:8090

# --- Process Killing ---
alias killfirefox = pkill -9 firefox
alias killslack = pkill -9 slack

# --- Case Aliases ---
alias CD = cd
alias Cd = cd

# --- Proxy Command Wrappers ---
# These use 'env' to set environment variables for just the wrapped command
# Requires $env.no_proxy to be set in env.nu
alias prox1089 = env https_proxy="socks5://127.0.0.1:1089" http_proxy="socks5://127.0.0.1:1089" no_proxy=$env.no_proxy
alias prox9550 = env https_proxy="socks5://127.0.0.1:9550" http_proxy="socks5://127.0.0.1:9550" no_proxy=$env.no_proxy 
alias prox9050 = env https_proxy="socks5://127.0.0.1:9050" http_proxy="socks5://127.0.0.1:9050" no_proxy=$env.no_proxy 
alias prox9150 = env https_proxy="socks5://127.0.0.1:9150" http_proxy="socks5://127.0.0.1:9150" no_proxy=$env.no_proxy 
alias prox8090 = env http_proxy="http://127.0.0.1:8090" https_proxy="http://127.0.0.1:8090" no_proxy=$env.no_proxy
alias prox8000 = env http_proxy="http://127.0.0.1:8000" https_proxy="http://127.0.0.1:8000" no_proxy=$env.no_proxy
alias prox8080 = env http_proxy="http://127.0.0.1:8080" https_proxy="http://127.0.0.1:8080" no_proxy=$env.no_proxy
alias prox8118 = env http_proxy="http://localhost:8118" https_proxy="http://localhost:8118" no_proxy=$env.no_proxy


# --- Temporary Directory ---
alias tmp = cd (mktemp -d)

# --- Editor Aliases ---
alias open = emacsclient -nc -s initd -a nvim
alias edit = emacsclient -nw -s initd -a nvim
alias svim = vim --clean
alias snvim = nvim --noplugin --clean

# --- Clipboard Alias ---
alias c = xclip -selection clipboard

# --- Git Alias ---
alias ggrep = git grep --heading --break -n

# --- TaskWarrior Alias ---
alias task-tui = taskwarrior-tui
alias tasktui = taskwarrior-tui

# --- Systemctl Aliases ---
alias ctl = sudo systemctl
alias restart = sudo systemctl restart
# alias status = sudo systemctl status # Fish was commented out

# --- IP Alias ---
alias ip = ip -c

# --- Firefox Fallback Alias ---
# Check if developer edition exists and standard firefox does not
if (which firefox-developer-edition | is-not-empty) and (which firefox | is-empty) {
    alias firefox = firefox-developer-edition
}

# --- Clipboard Script Aliases ---
# Assumes a 'clipboard' script/command exists
alias clipboard.paste = clipboard paste
alias clipboard.copy = clipboard copy
alias clipboard.history = clipboard history
alias cb.paste = clipboard.paste
alias cb.cp = clipboard.copy
alias cb.hist = clipboard.history

# --- Misc Aliases ---
alias ping = ping -DO
alias please = sudo
alias tog = sig toggle # Assumes 'sig' command exists

# --- Aliases for commands typically needing sudo ---
# Direct aliases instead of the Fish `unsudo` function
# Users will type 'pacman ...' and it will run 'sudo pacman ...'
alias pacman = sudo pacman
alias ausearch = sudo ausearch
alias reboot = sudo reboot
alias poweroff = sudo poweroff
alias auditctl = sudo auditctl
alias aureport = sudo aureport
alias dnstop = sudo dnstop
# alias nftop = sudo nftop # Command might not exist everywhere
alias nft = sudo nft
alias cpupower = sudo cpupower
alias apt = sudo apt
alias ufw = sudo ufw
alias strace = sudo strace

