#!/bin/bash

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