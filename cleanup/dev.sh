#!/bin/bash

trash "$share/CMakeTools"
trash "$share/org.gnome.Devhelp"
trash "$share/devhelp"
trash "$share/meld"
trash "$share/GitKrakenCLI"
trash "$share/DBeaverData"

for file in $(find ~/codeshells/ -type d -name "build" -or -type d -name "build-clang" -or -type d -name "cmake-build-*" -or -type d -name ".venv" -or -type d -name "node_modules" 2>/dev/null); do
    trash "$file"
done


trash "$HOME/.eclipse"
trash "$HOME/.duckdb"
trash "$HOME/.duckdb_history"
trash "$HOME/.rest-client" # VSCode Extension
trash "$HOME/.nuget"
trash "$HOME/.lldb"
trash "$HOME/.dotnet"
trash "$HOME/.gk" # GitKraken