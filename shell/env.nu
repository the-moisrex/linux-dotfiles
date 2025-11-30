# ~/.config/nushell/env.nu

# --- Basic Environment ---

# Set EDITOR (equivalent to: set -gx EDITOR "nvim")
$env.EDITOR = "nvim"

# Define custom command directory (equivalent to: set -gx cmddir "$HOME/cmd")
let home_dir = $env.HOME
$env.cmddir = $"($home_dir)/cmd"

# Set GPG_TTY (equivalent to: set -gx GPG_TTY "$(tty)")
$env.GPG_TTY = (tty)

# --- Build Flags ---

# Improving build times (equivalent to: set -gx MAKEFLAGS "-j$(nproc)")
$env.MAKEFLAGS = $"-j(nproc)"

# Linker flags (equivalent to: set -gx LDFLAGS "...")
$env.LDFLAGS = "-Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now -fuse-ld=mold"

# --- Cache ---

# CPM Cache (equivalent to: set -gx CPM_SOURCE_CACHE "$HOME/.cache/CPM")
$env.CPM_SOURCE_CACHE = $"($home_dir)/.cache/CPM"

# --- Java ---

# Set JAVA_HOME with fallback
# (equivalent to Fish JAVA_HOME logic)
let preferred_java_home = "/usr/lib/jvm/java-20-openjdk"
let fallback_java_home = "/usr/lib/jvm/default"
if (($preferred_java_home | path type) == dir) {
    $env.JAVA_HOME = $preferred_java_home
} else if (($fallback_java_home | path type) == dir) {
    $env.JAVA_HOME = $fallback_java_home
} else {
    # Optionally hide the variable if neither path exists and it might be inherited
    # hide-env JAVA_HOME
    # Or just don't set it. Let's not set it if neither exists.
}

# --- VCPkg ---

# Set VCPKG_ROOT if it exists
# (equivalent to Fish VCPKG_ROOT logic)
let vcpkg_root_path = "/opt/vcpkg"
if (($vcpkg_root_path | path type) == dir) {
    $env.VCPKG_ROOT = $vcpkg_root_path
} else {
    # Unset VCPKG_ROOT if the directory doesn't exist
    # If it might be inherited, hide it:
    # hide-env VCPKG_ROOT
}


# --- PATH Configuration ---
# Nushell's $env.PATH is a list. We modify it by prepending/appending.
# Note: Order matters. Define variables like JAVA_HOME *before* using them here.

# Start with the existing PATH inherited from the environment
$env.PATH = $env.PATH

# Prepend standard user binary locations and custom cmd dir
# (equivalent to ". : $HOME/.bin : $cmddir/bin : $cmddir/firewall" prepended to PATH)
$env.PATH = ($env.PATH | prepend [
    "."
    $"($home_dir)/.bin"
    $"($env.cmddir)/bin"
    $"($env.cmddir)/firewall"
])

# Append other locations
# (equivalent to appending $HOME/.local/bin:$JAVA_HOME/bin:$HOME/.cargo/bin:...)
let paths_to_append = [
    $"($home_dir)/.local/bin"
    # Conditionally add JAVA_HOME/bin
    if ($env.JAVA_HOME? | is-not-empty) { $"($env.JAVA_HOME)/bin" } else { null }
    $"($home_dir)/.cargo/bin"
    $"($home_dir)/Android/Sdk/platform-tools"
    $"($home_dir)/Android/Sdk/emulator"
]

# Filter out nulls (in case JAVA_HOME wasn't set) and append
$env.PATH = ($env.PATH | append ($paths_to_append | where {|it| $it != null}))


# --- LD_LIBRARY_PATH ---
# Fish's `set -gx LD_LIBRARY_PATH "." $LD_LIBRARY_PATH` is slightly ambiguous.
# It usually space-separates non-path list variables. LD_LIBRARY_PATH is typically colon-separated.
# Assuming the *intent* was standard colon separation like PATH:
# Prepend "." to LD_LIBRARY_PATH, creating it if it doesn't exist.
$env.LD_LIBRARY_PATH = $".:($env.LD_LIBRARY_PATH?)"


# --- Conda ---
# >>> conda initialize >>>
# !! Contents within this block are managed by 'conda init nu' !!
# Run `conda init nu` to have Conda manage this section automatically.
# The lazy-loading function from Fish is replaced by Conda's direct integration.
# <<< conda initialize <<<


# --- Project Roots ---
# $env.gitroot = "" # Initial value, function calc_git_root will find the real one if needed
$env.projects_root = $"($env.HOME)/Projects"

# --- LS_COLORS ---
# Set LS_COLORS based on dircolors (equivalent to eval (dircolors ...))
# Run dircolors and capture its output to set the environment variable
# try {
#     let dircolors_path = "~/.dircolors"
#     let ls_colors_output = if (($dircolors_path | path type) == file) {
#         dircolors -c $dircolors_path
#     } else {
#         dircolors -c
#     }
#     # The output of dircolors is the command to set LS_COLORS, parse it
#     # Example output: LS_COLORS='...'; export LS_COLORS
#     # We just need the value inside the quotes
#     let ls_colors_value = ($ls_colors_output | parse "LS_COLORS='{value}'; export LS_COLORS" | get value | first)
#     $env.LS_COLORS = $ls_colors_value
# } catch {
#     # Ignore if dircolors command fails or parsing fails
# }


# --- Proxy Settings ---
$env.no_proxy = "localhost,127.0.0.1/8,192.168.0.0/16,10.0.0.0/8,*.localhost"
