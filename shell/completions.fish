
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
