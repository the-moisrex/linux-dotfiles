# Database Stack with Podman Compose

This repository contains a comprehensive database stack deployed using Podman Compose. It includes multiple database technologies and their corresponding management interfaces, all configured with persistent storage and Traefik routing.

## Services Included

### Relational Databases
- **MariaDB** - MySQL-compatible database
- **PostgreSQL** - Advanced open-source relational database
- **Supabase** - Open-source Firebase alternative (PostgreSQL-based)

### NoSQL Databases
- **Neo4j** - Graph database
- **MongoDB** - Document-oriented database
- **Redis** - In-memory key-value store

### Analytical Databases
- **DuckDB** - Analytical database with Jupyter interface

### File-based Databases
- **SQLite** - Lightweight file-based database

### Management Interfaces
- **phpMyAdmin** - Web interface for MariaDB/MySQL
- **pgAdmin** - Web interface for PostgreSQL
- **Adminer** - Universal database management tool
- **Supabase Studio** - Web interface for Supabase projects

### Analytics & Visualization
- **Apache Superset** - Data visualization and exploration platform

## Prerequisites

- Podman
- Podman Compose plugin

## Setup Instructions

1. Clone this repository
2. Ensure you have Podman and Podman Compose installed
3. Run the following command to start all services:

```bash
podman-compose up -d
```

## Storage Locations

All databases store their data persistently in your home directory under the `databases` folder:

- `$HOME/databases/neo4j/` - Neo4j data
- `$HOME/databases/mariadb/` - MariaDB data
- `$HOME/databases/postgres/` - PostgreSQL data
- `$HOME/databases/sqlite/` - SQLite files
- `$HOME/databases/duckdb/` - DuckDB files
- `$HOME/databases/redis/` - Redis data
- `$HOME/databases/mongodb/` - MongoDB data
- `$HOME/databases/superset/` - Apache Superset data
- `$HOME/databases/supabase/` - Supabase data
- `$HOME/databases/pgadmin/` - pgAdmin data

## Access Information

All web interfaces are accessible via Traefik routing using `.localhost` domains:

- **Neo4j Browser**: [http://neo4j.localhost](http://neo4j.localhost) (Username: `root`, Password: `toor`)
- **MariaDB**: [http://mariadb.localhost](http://mariadb.localhost) (Direct connection)
- **phpMyAdmin**: [http://phpmyadmin.localhost](http://phpmyadmin.localhost) (Username: `root`, Password: `toor`)
- **PostgreSQL**: [http://postgres.localhost](http://postgres.localhost) (Direct connection)
- **pgAdmin**: [http://pgadmin.localhost](http://pgadmin.localhost) (Email: `admin@localhost`, Password: `toor`)
- **SQLite**: [http://sqlite.localhost](http://sqlite.localhost)
- **DuckDB**: [http://duckdb.localhost](http://duckdb.localhost) (Jupyter Lab)
- **Adminer**: [http://adminer.localhost](http://adminer.localhost) (Username: `root`, Password: `toor`)
- **Redis**: [http://redis.localhost](http://redis.localhost) (Direct connection)
- **MongoDB**: [http://mongodb.localhost](http://mongodb.localhost) (Direct connection)
- **Apache Superset**: [http://superset.localhost](http://superset.localhost)
- **Supabase DB**: [http://supabase-db.localhost](http://supabase-db.localhost) (Direct connection)
- **Supabase Studio**: [http://supabase-studio.localhost](http://supabase-studio.localhost)

## Default Credentials

For all services that require authentication, the default credentials are:

- **Username**: `root`
- **Password**: `toor`

## Ports Used

- 7474: Neo4j HTTP
- 7687: Neo4j Bolt
- 3306: MariaDB
- 5432: PostgreSQL
- 8080: phpMyAdmin
- 8081: SQLite Web Interface
- 8082: pgAdmin
- 8083: Adminer
- 8888: DuckDB (Jupyter)
- 6379: Redis
- 27017: MongoDB
- 8088: Apache Superset
- 54322: Supabase Database
- 54323: Supabase Studio

## Stopping the Services

To stop all services:

```bash
podman-compose down
```

To stop and remove all containers, networks, and volumes:

```bash
podman-compose down -v
```

## Customization

You can customize the configuration by modifying the `podman-compose.yml` file according to your needs. Common customizations include:

- Changing default passwords
- Modifying port mappings
- Adjusting resource limits
- Adding additional environment variables

## Troubleshooting

If you encounter issues:

1. Check that Podman and Podman Compose are properly installed
2. Verify that the required ports are not already in use
3. Check the logs with `podman-compose logs [service-name]`
4. Ensure you have sufficient disk space for the databases

## License

This configuration is provided as-is for educational and development purposes.