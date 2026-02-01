#!/bin/bash

# Wait for pgAdmin to be fully started
sleep 30

# Check if the server configurations have already been added
if sqlite3 /var/lib/pgadmin/pgadmin4.db "SELECT COUNT(*) FROM server;" | grep -q "^0$"; then
    echo "Adding preconfigured servers to pgAdmin..."
    
    # Install sqlite if not already installed
    apk add --no-cache sqlite > /dev/null 2>&1
    
    # Add the preconfigured servers to the database
    sqlite3 /var/lib/pgadmin/pgadmin4.db "INSERT OR IGNORE INTO server (user_id, servergroup_id, name, host, port, username, maintenance_db, comment) VALUES (1, 1, 'PostgreSQL Main', 'postgres', 5432, 'root', 'postgres', 'Main PostgreSQL database');"
    sqlite3 /var/lib/pgadmin/pgadmin4.db "INSERT OR IGNORE INTO server (user_id, servergroup_id, name, host, port, username, maintenance_db, comment) VALUES (1, 1, 'Supabase PostgreSQL', 'supabase-db', 5432, 'supabase_admin', 'postgres', 'Supabase PostgreSQL database');"
    
    echo "Server configurations added to pgAdmin database."
else
    echo "Server configurations already exist in pgAdmin database."
fi

# Keep the container running
exec tail -f /dev/null