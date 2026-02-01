# Database Configuration Setup

This directory contains scripts and configurations for the database management tools in the pod.

## Setup Script

The `setup_database_configs.sh` script creates all necessary directories and configuration files for the database management tools:

- Creates SQLite database files (main.db, test.db, development.db)
- Sets up pgAdmin server configurations
- Configures PHPMyAdmin with connections to all databases
- Creates authentication files

## Usage

Run the setup script whenever you recreate the pod or delete the `$HOME/databases` directory:

```bash
./setup_database_configs.sh
```

After running the script, start your pod with:

```bash
podman-compose -f podman-compose.yml up -d
```

After the services are running, configure pgAdmin with preconfigured servers:

```bash
./configure_pgadmin.sh
```

The database management tools will have all preconfigured connections ready to use.

## Services

Once the pod is running, access the database management tools at:

- **pgAdmin**: http://pgadmin.localhost or http://localhost:8082
- **PHPMyAdmin**: http://phpmyadmin.localhost or http://localhost:8084
- **Adminer**: http://adminer.localhost or http://localhost:8083
- **SQLite Admin**: http://sqlite.localhost or http://localhost:8081
- **MongoDB Admin**: http://mongo.localhost or http://localhost:8086
- **Redis Admin**: http://redis.localhost or http://localhost:8085
- **Neo4j Browser**: http://neo4j.localhost or http://localhost:7474