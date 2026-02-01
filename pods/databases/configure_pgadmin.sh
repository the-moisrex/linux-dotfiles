#!/bin/bash

echo "Configuring pgAdmin with preconfigured servers..."

# Wait for pgAdmin to be fully started
echo "Waiting for pgAdmin database to be ready..."
sleep 45

# Check if the pgAdmin container is running
if ! podman ps | grep -q pgadmin-web; then
    echo "Error: pgAdmin container is not running"
    exit 1
fi

# Install sqlite in the container if not present and add the server configurations
podman exec pgadmin-web sh -c "
if ! command -v sqlite3 &> /dev/null; then
    apk add --no-cache sqlite
fi

# Wait for the database file to exist and be accessible
counter=0
while [ ! -f /var/lib/pgadmin/pgadmin4.db ] && [ \$counter -lt 30 ]; do
    echo 'Waiting for pgAdmin database to be created...'
    sleep 2
    counter=\$((\$counter + 1))
done

if [ \$counter -ge 30 ]; then
    echo 'Timeout waiting for database file'
    exit 1
fi

# Small delay to ensure the DB is ready
sleep 10

# Check if servers are already configured
server_count=\$(sqlite3 /var/lib/pgadmin/pgadmin4.db \"SELECT COUNT(*) FROM server;\" 2>/dev/null || echo 0)

if [ \"\$server_count\" -eq 0 ]; then
    echo 'Adding preconfigured servers to pgAdmin...'
    
    # Add the preconfigured servers to the database
    sqlite3 /var/lib/pgadmin/pgadmin4.db \"INSERT OR IGNORE INTO server (user_id, servergroup_id, name, host, port, username, maintenance_db, comment) VALUES (1, 1, 'PostgreSQL Main', 'postgres', 5432, 'root', 'postgres', 'Main PostgreSQL database');\"
    sqlite3 /var/lib/pgadmin/pgadmin4.db \"INSERT OR IGNORE INTO server (user_id, servergroup_id, name, host, port, username, maintenance_db, comment) VALUES (1, 1, 'Supabase PostgreSQL', 'supabase-db', 5432, 'supabase_admin', 'postgres', 'Supabase PostgreSQL database');\"
    
    echo 'Server configurations added to pgAdmin database.'
else
    echo \"Found \$server_count existing server(s) in pgAdmin database.\"
fi
"

echo "pgAdmin configuration complete!"