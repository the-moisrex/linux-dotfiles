# ~/.config/nushell/config.nu
# config.nu
#
# Installed by:
# version = "0.103.0"
#
# This file is used to override default Nushell settings, define
# (or import) custom commands, or run any other startup tasks.
# See https://www.nushell.sh/book/configuration.html
#
# This file is loaded after env.nu and before login.nu
#
# You can open this file in your default editor using:
# config nu
#
# See `help config nu` for more options
#
# You can remove these comments if you want or leave
# them for future reference.

$env.config.buffer_editor = "nvim"
$env.config.show_banner = false

# History settings (equivalent to HISTSIZE/SAVEHIST)
$env.config.history.max_size = 100000000
$env.config.history.file_format = "sqlite" # Recommended format
$env.config.display_errors.exit_code = true


# --- Source Environment ---
# Load environment variables defined in env.nu
# This line is often present in the default config.nu; ensure it's there.
# source-env ~/.config/nushell/env.nu


# --- Hooks ---

# Play sound hook (equivalent to fish_postexec hook)
# First, ensure the `play_pipe_sound` command exists and works as expected.
# You might need to define it as a function or ensure it's in the PATH.

# Check if the command exists (optional, but good practice)
# let play_sound_cmd = "play_pipe_sound" # Or the full path if needed
# let play_sound_exists = (which $play_sound_cmd | is-not-empty)
# 
# if $play_sound_exists {
#     # Define the hook function
#     let play_sound_hook = {||
#         # Get the exit code of the last command
#         let last_exit_code = $env.LAST_EXIT_CODE
# 
#         if $last_exit_code != null { # Ensure there was a last command
#             # Print status (adjust color/format as needed)
#             print $"[yellow]►[reset] [ ($last_exit_code) ]"
# 
#             # Run the sound command in the background, ignore its output/errors
#             try {
#                 # Use start to run in background and detach
#                 start --background {|| run-external --redirect-stdout null --redirect-stderr null $play_sound_cmd $last_exit_code } | ignore
#             } catch {
#                 # Optionally log if the sound command fails
#                 # print $"Error running play_pipe_sound: ($err)"
#             }
#         }
#     }
# 
#     # Append the hook to the pre_prompt hook list
#     $env.config.hooks.pre_prompt = ($env.config.hooks.pre_prompt | default [] | append $play_sound_hook)
# }


# --- Source Custom Files ---
source ~/.config/nushell/env.nu
source ~/.config/nushell/aliases.nu
# source ~/.config/nushell/completions.nu

let fish_completer = {|spans|
    fish -i --command $'complete --do-complete "($spans | str join " " | str replace "\"" "\\\"")"'
    | from tsv --flexible --noheaders --no-infer
    | rename value description
    | update value {
        if ($in | path exists) {$'($in | str replace "\"" "\\\"" )'} else {$in}
    }
}

let carapace_completer = {|spans: list<string>|
    carapace $spans.0 nushell ...$spans
    | from json
    | if ($in | default [] | where value =~ '^-.*ERR$' | is-empty) { $in } else { null }
}

# let carapace_completer = {|spans|
#     carapace $spans.0 nushell ...$spans | from json
# }

# This completer will use carapace by default
let external_completer = {|spans|
    let expanded_alias = scope aliases
    | where name == $spans.0
    | get -i 0.expansion

    let spans = if $expanded_alias != null {
        $spans
        | skip 1
        | prepend ($expanded_alias | split row ' ' | take 1)
    } else {
        $spans
    }

    match $spans.0 {
        # carapace completions are incorrect for nu
        nu => $fish_completer
        # fish completes commits and branch names in a nicer way
        git => $fish_completer
        codeshell => $fish_completer
        telegram.links => $fish_completer
        cdi => $fish_completer
        cdproj => $fish_completer
        proj => $fish_completer
        run => $fish_completer
        runif => $fish_completer
        # carapace doesn't have completions for asdf
        asdf => $fish_completer
        # use zoxide completions for zoxide commands
        # __zoxide_z | __zoxide_zi => $zoxide_completer
        _ => $carapace_completer
    } | do $in $spans
}

$env.config = ($env.config? | default {} | merge {
  completions: {
    external: {
      enable: true
      completer: $external_completer
      max_results: 100
    }
    case_sensitive: false # set to true to enable case-sensitive completions
    quick: true  # set this to false to prevent auto-selecting completions when only one remains
    partial: true  # set this to false to prevent partial filling of the prompt
    algorithm: "prefix"  # prefix, fuzzy
  }
})

