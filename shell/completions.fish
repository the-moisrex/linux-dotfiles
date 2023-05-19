
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
