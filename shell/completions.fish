
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

# execif
# complete -c execif -d "Executable Name" \
#     -n "[ (commandline | wc -l) = '1' ]" \
#     -n "[ (commandline --tokenize | wc -l) != '1' ]" \
#     -xa "three four yay"
#     -xa '(set -l t (commandline -ct | string split " " -f2); complete -C "$t")'
#     -xa '(set -l t (commandline -opc)[2..-1]; complete -C "$t")'
complete -c execif -d "Executable Arguments" \
    -xa '(set -l t (commandline | string replace "execif " ""); complete -C "$t")'
