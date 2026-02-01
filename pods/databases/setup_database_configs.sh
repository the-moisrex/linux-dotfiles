#!/bin/bash

set -e  # Exit on any error

echo "Setting up database configurations..."

# Create the databases directory structure
mkdir -p $HOME/databases/{pgadmin,servers,sqlite,mongodb,redis,neo4j}

# Create the SQLite database files
touch $HOME/databases/sqlite/main.db
touch $HOME/databases/sqlite/test.db
touch $HOME/databases/sqlite/development.db

# Create the pgAdmin servers directory
mkdir -p $HOME/databases/pgadmin/servers

# Create the PHPMyAdmin configuration
cat > $HOME/databases/phpmyadmin_config.inc.php << 'EOF'
<?php
/**
 * phpMyAdmin configuration file
 */

// Servers configuration
$i = 0;

// PostgreSQL server (via pga_proxy container)
$i++;
$cfg['Servers'][$i]['auth_type'] = 'config';
$cfg['Servers'][$i]['host'] = 'postgres';
$cfg['Servers'][$i]['port'] = 5432;
$cfg['Servers'][$i]['user'] = 'root';
$cfg['Servers'][$i]['password'] = 'toor';
$cfg['Servers'][$i]['connect_type'] = 'tcp';
$cfg['Servers'][$i]['compress'] = false;
$cfg['Servers'][$i]['AllowNoPassword'] = false;
$cfg['Servers'][$i]['verbose'] = 'PostgreSQL Main';

// MariaDB server
$i++;
$cfg['Servers'][$i]['auth_type'] = 'config';
$cfg['Servers'][$i]['host'] = 'mariadb';
$cfg['Servers'][$i]['port'] = 3306;
$cfg['Servers'][$i]['user'] = 'root';
$cfg['Servers'][$i]['password'] = 'toor';
$cfg['Servers'][$i]['connect_type'] = 'tcp';
$cfg['Servers'][$i]['compress'] = false;
$cfg['Servers'][$i]['AllowNoPassword'] = false;
$cfg['Servers'][$i]['verbose'] = 'MariaDB Main';

// MySQL server
$i++;
$cfg['Servers'][$i]['auth_type'] = 'config';
$cfg['Servers'][$i]['host'] = 'mysql';
$cfg['Servers'][$i]['port'] = 3306;
$cfg['Servers'][$i]['user'] = 'root';
$cfg['Servers'][$i]['password'] = 'toor';
$cfg['Servers'][$i]['connect_type'] = 'tcp';
$cfg['Servers'][$i]['compress'] = false;
$cfg['Servers'][$i]['AllowNoPassword'] = false;
$cfg['Servers'][$i]['verbose'] = 'MySQL Main';

// Supabase PostgreSQL server
$i++;
$cfg['Servers'][$i]['auth_type'] = 'config';
$cfg['Servers'][$i]['host'] = 'supabase-db';
$cfg['Servers'][$i]['port'] = 5432;
$cfg['Servers'][$i]['user'] = 'supabase_admin';
$cfg['Servers'][$i]['password'] = 'toor';
$cfg['Servers'][$i]['connect_type'] = 'tcp';
$cfg['Servers'][$i]['compress'] = false;
$cfg['Servers'][$i]['AllowNoPassword'] = false;
$cfg['Servers'][$i]['verbose'] = 'Supabase PostgreSQL';

// Enable connection collation setting
$cfg['Servers'][$i]['connection_collation'] = 'utf8_general_ci';

// Set default db for each server
$cfg['Servers'][$i]['pmadb'] = '';
$cfg['Servers'][$i]['relation'] = '';
$cfg['Servers'][$i]['table_info'] = '';
$cfg['Servers'][$i]['table_coords'] = '';
$cfg['Servers'][$i]['pdf_pages'] = '';

/* 
 * End of servers configuration
 */

// phpMyAdmin settings
$cfg['blowfish_secret'] = 'uselesstricksforlife';
$cfg['DefaultLang'] = 'en';
$cfg['ServerDefault'] = 1;
$cfg['UploadDir'] = '';
$cfg['SaveDir'] = '';
$cfg['VersionCheck'] = false;
?>
EOF

# Create the pgAdmin servers configuration (this is for reference, actual config is done in container)
mkdir -p $HOME/databases/pgadmin/servers
cat > $HOME/databases/pgadmin/servers/servers.json << 'EOF'
{
    "Servers": {
        "1": {
            "Name": "PostgreSQL Main",
            "Group": "Servers",
            "Host": "postgres",
            "Port": 5432,
            "MaintenanceDB": "postgres",
            "Username": "root",
            "Comment": "Main PostgreSQL database"
        },
        "2": {
            "Name": "Supabase PostgreSQL",
            "Group": "Servers",
            "Host": "supabase-db",
            "Port": 5432,
            "MaintenanceDB": "postgres",
            "Username": "supabase_admin",
            "Comment": "Supabase PostgreSQL database"
        }
    }
}
EOF

# Create the pgpass file for authentication
cat > $HOME/databases/pgpassfile << 'EOF'
postgres:5432:*:root:toor
supabase-db:5432:*:supabase_admin:toor
EOF

chmod 600 $HOME/databases/pgpassfile

# Create the pgAdmin custom configuration
cat > $HOME/databases/pgadmin_config.py << 'EOF'
import os
DATA_DIR = '/var/lib/pgadmin'
LOG_FILE = os.path.join(DATA_DIR, 'pgadmin4.log')
SQLITE_TIMEOUT = 60
SERVER_MODE = False
MASTER_PASSWORD_REQUIRED = False
EOF

# Create the SQLite setup script
cat > $HOME/databases/setup_sqlite.sh << 'EOF'
#!/bin/bash
# Setup script for SQLite databases

# Create sample tables in main.db
sqlite3 $HOME/databases/sqlite/main.db << 'SQL'
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT UNIQUE NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS products (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    price REAL NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

INSERT OR IGNORE INTO users (name, email) VALUES ('John Doe', 'john@example.com');
INSERT OR IGNORE INTO users (name, email) VALUES ('Jane Smith', 'jane@example.com');
INSERT OR IGNORE INTO products (name, price) VALUES ('Laptop', 999.99);
INSERT OR IGNORE INTO products (name, price) VALUES ('Mouse', 29.99);
SQL

# Create sample tables in test.db
sqlite3 $HOME/databases/sqlite/test.db << 'SQL'
CREATE TABLE IF NOT EXISTS test_table (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    data TEXT NOT NULL,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);

INSERT OR IGNORE INTO test_table (data) VALUES ('Test record 1');
INSERT OR IGNORE INTO test_table (data) VALUES ('Test record 2');
SQL

# Create sample tables in development.db
sqlite3 $HOME/databases/sqlite/development.db << 'SQL'
CREATE TABLE IF NOT EXISTS dev_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    message TEXT NOT NULL,
    level TEXT DEFAULT 'info',
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);

INSERT OR IGNORE INTO dev_logs (message, level) VALUES ('Application started', 'info');
INSERT OR IGNORE INTO dev_logs (message, level) VALUES ('Debugging in progress', 'debug');
SQL

echo "SQLite databases have been initialized with sample data."
EOF

chmod +x $HOME/databases/setup_sqlite.sh

# Run the SQLite setup script
$HOME/databases/setup_sqlite.sh

echo "Database configurations have been set up successfully!"
echo "Directories and files created:"
echo "- $HOME/databases/pgadmin/"
echo "- $HOME/databases/sqlite/ with main.db, test.db, development.db"
echo "- $HOME/databases/phpmyadmin_config.inc.php"
echo "- $HOME/databases/pgadmin/servers/servers.json"
echo "- $HOME/databases/pgpassfile"
echo "- $HOME/databases/pgadmin_config.py"
echo "- $HOME/databases/setup_sqlite.sh"

echo ""
echo "When you recreate the pod, just run this script again to restore all configurations."