# Disable downloading
set debuginfod enabled off
set debuginfod urls
maintenance set debuginfod download-sections off
set tcp auto-retry off

# skip std namespace in C++
skip -rfu ^std::

set startup-with-shell off
set breakpoint pending on
set auto-load safe-path /
set debug-file-directory ""

add-auto-load-safe-path ~/cmd/gdb/
set auto-load scripts-directory ~/cmd/gdb/

source ~/cmd/gdb/chains.gdb
