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
source ~/.config/nushell/completions.nu

