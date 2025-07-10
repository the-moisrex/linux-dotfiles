set debuginfod enabled off
set startup-with-shell off
set breakpoint pending on

add-auto-load-safe-path ~/cmd/gdb/
set auto-load scripts-directory ~/cmd/gdb/
source ~/cmd/gdb/all.gdb
