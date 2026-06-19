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
set print pretty on

add-auto-load-safe-path ~/cmd/gdb/
set auto-load scripts-directory ~/cmd/gdb/

source ~/cmd/gdb/chains.gdb
source ~/cmd/gdb/stl-views-1.0.3.gdb
