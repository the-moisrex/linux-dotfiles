# completions.nu - Nushell completions for external commands
# Fully structured + filtered completions

# === Helper: filter completions by user input ===
def filter_completions [word: string, list: list<record<value: string, description: string>>] {
    $list | where {|it| $it.value | str starts-with $word }
}

# === Helper: get git root ===
def get_git_root [] {
    try { git rev-parse --show-toplevel | str trim } catch { $nu.home-path }
}

# === codeshell completion ===
def complete_codeshell [spans: list<string>] {
    let word = ($spans | last | default "")
    let base = if $word starts-with "-" {
        [
            {value: "-n", description: "Name of the subject"},
            {value: "--name", description: "Name of the subject"},
            {value: "-t", description: "Template to use"},
            {value: "--template", description: "Template to use"},
            {value: "-g", description: "CMake generator"},
            {value: "-G", description: "CMake generator"}
        ]
    } else {
        let projects = try { ls $"($nu.home-path)/codeshells" | where type == dir | get name | path basename } catch { [] }
        let templates = try { ls $"($nu.home-path)/cmd/code-templates" | where type == dir | get name | path basename } catch { [] }
        ($projects | append $templates | uniq) | each {|it| {value: $it, description: "Project or template"}}
    }
    filter_completions $word $base
}

# === run completion ===
def complete_run [spans: list<string>] {
    let word = ($spans | last | default "")
    let targets = try { run print-targets | lines } catch { [] }
    let static = [
        {value: "lldb", description: "Debug in lldb"},
        {value: "gdb", description: "Debug in gdb"},
        {value: "less", description: "Pipe results to less"},
        {value: "print-targets", description: "Print available targets"},
        {value: "help", description: "Show help"}
    ]
    let all = ($targets | each {|t| {value: $t, description: "Run target"}}) ++ $static
    filter_completions $word $all
}

# === runif completion ===
def complete_runif [spans: list<string>] {
    let word = ($spans | last | default "")
    let paths = ($env.PATH | split row (char esep))
    let exes = ($paths
        | each {|p|
            try { ls $"($p)" | where type == file | get name } catch { [] }
        }
        | flatten
        | uniq
        | each {|n| {value: $n, description: "Executable"}}
    )
    filter_completions $word $exes
}

# === cdi completion ===
def complete_cdi [spans: list<string>] {
    let word = ($spans | last | default "")
    let gitroot = get_git_root
    let dirs = try { ls $"($gitroot)" | where type == dir | get name | each {|n| {value: $n, description: "Directory"}} } catch { [] }
    filter_completions $word $dirs
}

# === cdproj completion ===
def complete_cdproj [spans: list<string>] {
    let word = ($spans | last | default "")
    let dirs = if 'projects_root' in $env {
        try { ls $"($env.projects_root)" | where type == dir | get name | path basename | each {|n| {value: $n, description: "Project directory"}} } catch { [] }
    } else { [] }
    filter_completions $word $dirs
}

# === proj completion ===
def complete_proj [spans: list<string>] {
    let word = ($spans | last | default "")
    # Use the same logic as in aliases.nu to determine projects_root
    let projects_root = if $env.projects_root? != null { $env.projects_root } else { [$nu.home-path "Projects"] | path join }
    let dirs = try {
        ls $"($projects_root)" | where type == dir | get name | path basename | each {|n| {value: $n, description: "Project directory"}}
    } catch { [] }
    filter_completions $word $dirs
}

# === telegram.links completion ===
def complete_telegram_links [spans: list<string>] {
    let word = ($spans | last | default "")
    let cmdline = ($spans | str join " ")
    let parts = ($spans | where {|x| $x != ""})

    let base = if ($word starts-with "-") {
        [
            {value: "-h", description: "Show help"},
            {value: "--help", description: "Show help"},
            {value: "--history", description: "Show history for a Telegram ID"},
            {value: "--all", description: "Show history for all IDs"},
            {value: "--clear-ids", description: "Clear stored Telegram IDs"},
            {value: "--clear-links", description: "Clear stored link history"},
            {value: "--clear-all", description: "Clear all IDs and links"},
            {value: "--list-ids", description: "List stored Telegram IDs"}
        ]
    } else if ($parts | length) > 1 and ($parts | last 2 | first) == "--history" {
        try { open $"($nu.home-path)/.config/telegram.links/ids.txt" | lines | each {|id| {value: $id, description: "Telegram ID"}} } catch { [] }
    } else {
        try { telegram.links --list-ids | lines | each {|id| {value: $id, description: "Telegram ID"}} } catch { [] }
    }

    filter_completions $word $base
}

# === prompt completion ===
def complete_prompt [spans: list<string>] {
    let word = ($spans | last | default "")
    let static = [
        {value: "-h", description: "Show help"},
        {value: "--help", description: "Show help"},
        {value: "list", description: "List available prompts"}
    ]
    let prompt_names = try { prompt list 2>/dev/null | lines | each {|p| {value: $p, description: "Prompt name"}} } catch { [] }
    filter_completions $word ($static ++ $prompt_names)
}

# === Fish & Carapace bridges ===
let fish_completer = {|spans|
    fish -i --command $'complete --do-complete "($spans | str join " " | str replace "\"" "\\\"")"'
    | from tsv --flexible --noheaders --no-infer
    | rename value description
}

let carapace_completer = {|spans: list<string>|
    carapace $spans.0 nushell ...$spans | from json
}

# === Unified external completer ===
let external_completer = {|spans|
    match $spans.0 {
        "codeshell" => (complete_codeshell $spans),
        "run" => (complete_run $spans),
        "runif" => (complete_runif $spans),
        "cdi" => (complete_cdi $spans),
        "cdproj" => (complete_cdproj $spans),
        "proj" => (complete_proj $spans),
        "telegram.links" => (complete_telegram_links $spans),
        "prompt" => (complete_prompt $spans),

        # Fall back to Fish-based completions for common tools
        "git" => (do $fish_completer $spans),
        "nu" => (do $fish_completer $spans),
        "asdf" => (do $fish_completer $spans),
        _ => (do $fish_completer $spans)
    }
}

# === Register completions in $env.config ===
$env.config = ($env.config? | default {} | merge {
  completions: {
    external: {
      enable: true
      completer: $external_completer
      max_results: 100
    }
    case_sensitive: false
    quick: true
    partial: true
    algorithm: "prefix"  # can be "fuzzy" for broader matching
  }
})

