# completions.nu - Nushell completions for external commands

# Completion for codeshell
def complete_codeshell [word: string] {
    if $word starts-with "-" {
        # Suggest options when the word starts with a dash
        [
            "-n", "--name",     # Name of the subject
            "-t", "--template", # The template to use
            "-g", "-G"          # The Build System to use in cmake
        ]
    } else {
        # Suggest project and template directories as positional arguments
        let projects = try { ls $"($nu.home-path)/codeshells/" | where type == dir | get name | path basename } catch { [] }
        let templates = try { ls $"($nu.home-path)/cmd/code-templates/" | where type == dir | get name | path basename } catch { [] }
        print ($projects | append $templates)
    }
}

# Completion for run
def complete_run [word: string] {
    # Dynamic targets from 'run print-targets'
    let targets = try { run print-targets | lines } catch { [] }
    # Static completion options
    let static = [
        "lldb",          # Debug in lldb
        "gdb",           # Debug in gdb
        "less -r -l l",  # Pipe the results to 'less'
        "print-targets", # Print Targets
        "help"           # Print Help
    ]
    $targets ++ $static
}

# Completion for runif
def complete_runif [word: string, cmdline: string] {
    # Suggest executable files in PATH as a basic implementation
    $env.PATH | split row (char esep) | each { |p| ls $"($p)/*" } | flatten | where type == file and name =~ '\.(exe|sh|bat)$' | get name
}

# Helper function to get git root
def get_git_root [] {
    try { git rev-parse --show-toplevel } catch { $nu.home-path }
}

# Completion for cdi
def complete_cdi [word: string] {
    let gitroot = get_git_root
    try { ls $"($gitroot)/*" | where type == dir | get name } catch { [] }
}

# Completion for cdproj
def complete_cdproj [word: string] {
    if 'projects_root' in $env {
        try { ls $"($env.projects_root)/*" | where type == dir | get name } catch { [] }
    } else {
        []
    }
}

# Completion for proj
def complete_proj [word: string] {
    if 'projects_root' in $env {
        try { ls $"($env.projects_root)/*" | where type == dir | get name } catch { [] }
    } else {
        []
    }
}

# Completion for telegram.links
def complete_telegram_links [word: string, cmdline: string] {
    let parts = $cmdline | split row " " | where $it != ""
    if $parts.-2? == "--history" {
        # Completions for --history argument from ids.txt
        try { open $"($nu.home-path)/.config/telegram.links/ids.txt" | lines } catch { [] }
    } else if $word starts-with "-" {
        # Suggest options when the word starts with a dash
        [
            "-h", "--help",        # Show help message and exit
            "--history",           # Show history of links for a specific Telegram ID
            "--all",               # Show history of links for all Telegram IDs
            "--clear-ids",         # Clear the list of stored Telegram IDs
            "--clear-links",       # Clear all stored Telegram links history
            "--clear-all",         # Clear both Telegram IDs and links history
            "--list-ids"           # List the stored Telegram IDs
        ]
    } else {
        # Suggest Telegram IDs as positional arguments
        try { telegram.links --list-ids | lines } catch { [] }
    }
}

# Central external completer
def external_completer [cmd: string, word: string, cmdline: string] {
    match $cmd {
        "codeshell" => (complete_codeshell $word),
        "run" => (complete_run $word),
        "runif" => (complete_runif $word $cmdline),
        "cdi" => (complete_cdi $word),
        "cdproj" => (complete_cdproj $word),
        "proj" => (complete_proj $word),
        "telegram.links" => (complete_telegram_links $word $cmdline),
        _ => [],  # No completions for unrecognized commands
    }
}
