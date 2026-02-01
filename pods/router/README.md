# Router Configuration

This directory contains the Traefik reverse proxy configuration and the dashboard service for managing all your podman compose files.

## Services

- **Traefik**: Reverse proxy and load balancer with dashboard at `http://traefik.localhost`
- **Dashboard**: Service that scans and displays all compose files in the project at `http://dashboard.localhost`

## Updating

To update the services after making changes to compose files or the dashboard:

```bash
# Navigate to the router directory
cd /path/to/cmd/pods/router

# Run the update script
./update.sh
```

### Update Script Options

- `./update.sh` - Standard update
- `./update.sh --force` - Force rebuild and restart without prompts
- `./update.sh --clean` - Clean up old containers and images before updating
- `./update.sh --help` - Show help information

## Prerequisites

Make sure you have the following entries in your `/etc/hosts` file:

```
127.0.0.1 dashboard.localhost
127.0.0.1 traefik.localhost
```

## Requirements

- Podman
- Podman Compose
- Python 3.6+

## Troubleshooting

If services are not accessible:
1. Verify that podman is running: `systemctl status podman`
2. Check that the hosts entries are in `/etc/hosts`
3. Verify that no other services are using port 80
4. Run `podman-compose ps` to check service status