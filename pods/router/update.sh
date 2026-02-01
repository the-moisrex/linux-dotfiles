#!/bin/bash

# Update script for Podman Compose Dashboard
# This script rebuilds and restarts the dashboard service when changes are detected

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "🔄 Starting update process for Podman Compose Dashboard..."

# Function to display usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  -f, --force    Force rebuild and restart without prompts"
    echo "  -c, --clean    Clean up old containers and images before updating"
    echo ""
    echo "Examples:"
    echo "  $0             # Standard update"
    echo "  $0 --force     # Force update without prompts"
    echo "  $0 --clean     # Clean and update"
}

# Parse command line arguments
FORCE_UPDATE=false
CLEAN_MODE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -f|--force)
            FORCE_UPDATE=true
            shift
            ;;
        -c|--clean)
            CLEAN_MODE=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Check if podman-compose is available
if ! command -v podman-compose &> /dev/null; then
    echo "❌ podman-compose is not installed or not in PATH"
    echo "Install it with: sudo apt install podman-compose  # On Debian/Ubuntu"
    echo "Or: sudo dnf install podman-docker              # On Fedora/RHEL"
    exit 1
fi

# Navigate to router directory
cd "$SCRIPT_DIR"

echo "📁 Working directory: $SCRIPT_DIR"

# Clean up old containers and images if requested
if [ "$CLEAN_MODE" = true ]; then
    echo "🧹 Cleaning up old containers and images..."
    
    # Stop and remove existing containers
    if podman-compose ps -q | grep -q .; then
        echo "Stopping existing containers..."
        podman-compose down || true
    fi
    
    # Remove unused images (optional - uncomment if needed)
    # echo "Removing unused images..."
    # podman image prune -f || true
    
    echo "✅ Cleanup completed"
fi

# Check if dashboard container is running and get its ID
DASHBOARD_CONTAINER_ID=$(podman ps -q --filter name=dashboard)

if [ -n "$DASHBOARD_CONTAINER_ID" ]; then
    echo "⏸️ Stopping existing dashboard container..."
    podman stop "$DASHBOARD_CONTAINER_ID" || true
fi

# Check if we need to rebuild the dashboard image
if [ "$FORCE_UPDATE" = true ] || [ ! -f "$PROJECT_ROOT/dashboard/index.html" ]; then
    echo "🔨 Rebuilding dashboard..."
    cd "$PROJECT_ROOT/dashboard"
    python3 generate_dashboard.py
    cd "$SCRIPT_DIR"
else
    echo "📊 Dashboard files already exist, skipping regeneration"
fi

# Build and start the services
echo "🏗️ Building and starting services..."
podman-compose up -d --build

# Wait a moment for services to start
echo "⏳ Waiting for services to start..."
sleep 5

# Check if services are running
echo "🔍 Checking service status..."
podman-compose ps

# Get the IP address of the host
if command -v hostname &> /dev/null && hostname -I &> /dev/null; then
    HOST_IP=$(hostname -I | awk '{print $1}')
elif command -v ip &> /dev/null; then
    HOST_IP=$(ip route get 1.1.1.1 | awk '{print $7; exit}')
else
    HOST_IP="localhost"
fi

if [ -z "$HOST_IP" ]; then
    HOST_IP="localhost"
fi

echo ""
echo "🎉 Update completed successfully!"
echo ""
echo "🌐 Access your services at:"
echo "   Dashboard: http://dashboard.localhost"
echo "   Traefik:   http://traefik.localhost"
echo ""
echo "💡 Tip: Make sure you have the following entries in your /etc/hosts file:"
echo "   $HOST_IP dashboard.localhost"
echo "   $HOST_IP traefik.localhost"
echo ""

# Check if /etc/hosts has the required entries
if ! grep -q "dashboard.localhost" /etc/hosts 2>/dev/null || ! grep -q "traefik.localhost" /etc/hosts 2>/dev/null; then
    echo "⚠️  Warning: Host entries may be missing from /etc/hosts"
    echo "   You may need to add these entries manually or run with sudo privileges."
fi

echo "✅ Process completed!"