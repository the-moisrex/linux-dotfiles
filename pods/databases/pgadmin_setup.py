#!/usr/bin/env python3
import sqlite3
import os
import sys

# Path to the pgAdmin config database
config_db_path = '/var/lib/pgadmin/pgadmin4.db'

# Wait for the database to be created
import time
while not os.path.exists(config_db_path):
    print("Waiting for pgAdmin config database to be created...")
    time.sleep(2)

print("Config database found, connecting...")

# Connect to the database
conn = sqlite3.connect(config_db_path)
cursor = conn.cursor()

try:
    # Insert server configurations
    cursor.execute("""
        INSERT OR IGNORE INTO server (
            user_id, servergroup_id, name, host, port, username, 
            maintenance_db, comment, passfile
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        1,  # user_id (default admin user)
        1,  # servergroup_id (default server group)
        'PostgreSQL Main',
        'postgres',  # host
        5432,  # port
        'root',  # username
        'postgres',  # maintenance_db
        'Main PostgreSQL database',  # comment
        '/pgpassfile'  # passfile
    ))

    cursor.execute("""
        INSERT OR IGNORE INTO server (
            user_id, servergroup_id, name, host, port, username, 
            maintenance_db, comment, passfile
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        1,  # user_id (default admin user)
        1,  # servergroup_id (default server group)
        'Supabase PostgreSQL',
        'supabase-db',  # host
        5432,  # port
        'supabase_admin',  # username
        'postgres',  # maintenance_db
        'Supabase PostgreSQL database',  # comment
        '/pgpassfile_supabase'  # passfile
    ))

    conn.commit()
    print("Server configurations inserted successfully.")
    
except Exception as e:
    print(f"Error inserting server configurations: {e}")
    sys.exit(1)
finally:
    conn.close()