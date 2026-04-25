
# codeshell
complete -c codeshell -s n -l name -d "Name of the subject"
complete -c codeshell -s t -l template -d "The tempalte to use"
complete -c codeshell -s g -s G -d "The Build System to use in cmake"
complete -x -c codeshell -d "Project" -a "(find \$HOME/codeshells/ -maxdepth 1 -type d -printf \"%P\n\")"
complete -x -c codeshell -d "Template" -a "(find \$HOME/cmd/code-templates/ -maxdepth 1 -type d -printf \"%P\n\")"

# run
complete -x -c run -a "(run print-targets)"
complete -c run -a lldb -d "Debug in lldb"
complete -c run -a gdb -d "Debug in gdb"
complete -c run -a watch -d "Watch for changes"
complete -c run -a 'less -r -l l' -d "Pipe the results to 'less'"
complete -c run -a 'print-targets' -d "Print Targets"
complete -c run -a help -d "Print Help"

# runif
# complete -c runif -d "Executable Name" \
#     -n "[ (commandline | wc -l) = '1' ]" \
#     -n "[ (commandline --tokenize | wc -l) != '1' ]" \
#     -xa "three four yay"
#     -xa '(set -l t (commandline -ct | string split " " -f2); complete -C "$t")'
#     -xa '(set -l t (commandline -opc)[2..-1]; complete -C "$t")'
complete -c runif -d "Executable Arguments" \
    -xa '(set -l t (commandline | string replace "runif " ""); complete -C "$t")'

function get_dirs_of
    set -l root (realpath --relative-to=. "$argv")
    __fish_complete_directories "$root/"(commandline -ct) | string replace "$root/" ""
end

function get_git_dirs
    calc_git_root
    # ls --oneline --no-symlink --almost-all --color=never --icon=never --sort=git --directory-only "$gitroot"/*/ | string replace "$gitroot" "" | string replace "/" ""
    get_dirs_of "$gitroot"
end

complete -x -c cdi -d "Subdirs" -a '(get_git_dirs)'
complete -x -c cdproj -d "Project Dirs" -a '(get_dirs_of $projects_root)'
complete -x -c proj   -d "Project Dirs" -a '(get_dirs_of $projects_root)'



# telegram.links
function __fish_complete_telegram_ids
    cat ~/.config/telegram.links/ids.txt
end
complete -c telegram.links -f -s h -l help -d "Show help message and exit"
complete -c telegram.links -f -l history -r -x -a "(__fish_complete_telegram_ids)" -d "Show history of links for a specific Telegram ID"
complete -c telegram.links -f -l all -d "Show history of links for all Telegram IDs"
complete -c telegram.links -f -l clear-ids -d "Clear the list of stored Telegram IDs"
complete -c telegram.links -f -l clear-links -d "Clear all stored Telegram links history"
complete -c telegram.links -f -l clear-all -d "Clear both Telegram IDs and links history"
complete -c telegram.links -f -l list-ids -d "List the stored Telegram IDs"

# Completion for main arguments (Telegram IDs/URLs) - No condition for now, applies always when no option is given.
complete -c telegram.links -f -a "(telegram.links --list-ids)" -d "Telegram Channel ID"

# prompt
function __fish_prompt_needs_name
    set -l tokens (commandline -opc)
    test (count $tokens) -le 1
end

function __fish_prompt_is_run
    set -l tokens (commandline -opc)
    test (count $tokens) -ge 2; and test "$tokens[2]" = run
end

function __fish_prompt_has_name
    set -l tokens (commandline -opc)
    test (count $tokens) -ge 2
end

function __fish_prompt_supports_head
    set -l tokens (commandline -opc)
    test (count $tokens) -ge 2
    or return 1
    contains -- "$tokens[2]" spp files symbols stupid security tests refactor api review fix comments explain perf commit
end

complete -c prompt -n '__fish_prompt_needs_name' -s h -l help -d "Show help message"
complete -c prompt -n '__fish_prompt_needs_name' -xa "list" -d "List available prompts"
complete -c prompt -n '__fish_prompt_needs_name' -xa "list-prompts" -d "List prompt names only"
# Only complete prompt names for the first positional argument so later args can fall back to file completion.
complete -c prompt -n '__fish_prompt_needs_name' -xa '(prompt list-prompts 2>/dev/null)' -d "Prompt name"
complete -c prompt -n '__fish_prompt_has_name' -s h -l help -d "Show help for the selected prompt"
complete -c prompt -n '__fish_prompt_supports_head' -l head -x -d "Keep only the first N lines of embedded context"
complete -c prompt -n '__fish_prompt_is_run' -l head -x -d "Trim run output with head"
complete -c prompt -n '__fish_prompt_is_run' -a '(run print-targets)' -d "Run target"
complete -c prompt -n '__fish_prompt_is_run' -a lldb -d "Debug in lldb"
complete -c prompt -n '__fish_prompt_is_run' -a gdb -d "Debug in gdb"
complete -c prompt -n '__fish_prompt_is_run' -a watch -d "Watch for changes"
complete -c prompt -n '__fish_prompt_is_run' -a 'less -r -l l' -d "Pipe the results to less"
complete -c prompt -n '__fish_prompt_is_run' -a print-targets -d "Print Targets"
complete -c prompt -n '__fish_prompt_is_run' -a help -d "Print Help"



# fish completion for detatch

# If no command has been given yet, complete with available commands
complete -c detatch -f -n "not __fish_seen_subcommand_from (__fish_complete_subcommand)" -a "(__fish_complete_subcommand)"

# After the first argument (the command), fall back to normal commandline completion
complete -c detatch -f -n "__fish_seen_subcommand_from (__fish_complete_subcommand)" -a "(__fish_complete_subcommand)"
