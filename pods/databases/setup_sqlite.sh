#!/bin/bash

show_help() {
    cat <<'EOF'
Usage: setup_sqlite.sh

Create default SQLite database files and directories.

This script creates the SQLite directory and default database files
if they don't already exist.

Options:
  -h, --help    Show this help message
EOF
}

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    show_help
    exit 0
fi

# Create SQLite directory if it doesn't exist
mkdir -p $HOME/databases/sqlite

# Create default SQLite database files if they don't exist
if [ ! -f "$HOME/databases/sqlite/main.db" ]; then
    touch $HOME/databases/sqlite/main.db
fi

if [ ! -f "$HOME/databases/sqlite/test.db" ]; then
    touch $HOME/databases/sqlite/test.db
fi

if [ ! -f "$HOME/databases/sqlite/development.db" ]; then
    touch $HOME/databases/sqlite/development.db
fi