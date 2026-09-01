#!/bin/bash

show_help() {
    cat <<'EOF'
Usage: install-services.sh [service...]

Install systemd services (user or system) by creating symlinks.

Arguments:
  service...    Service names or paths to .service files to install

Services are searched in this order:
  1. As absolute/relative path
  2. services/user/<name>
  3. services/user/<name>.service
  4. services/system/<name>
  5. services/system/<name>.service

Examples:
  install-services.sh myservice
  install-services.sh services/user/myservice.service
EOF
}

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    show_help
    exit 0
fi

dir=$(dirname "$0")
services="$@"

if [ $# -eq 0 ]; then
    echo "Error: No services specified." >&2
    show_help
    exit 1
fi

echo "Source file directory: $dir"
echo "Selected services to install: $@"
echo

for service in $services; do

    service_file="$service"
    user_service=false

    if [ -f "$service_file" ]; then
        user_service=false
        service_file="$service"
    elif [ -f "$dir/user/$service" ]; then
        service_file="$dir/user/$service"
        user_service=true
    elif [ -f "$dir/user/$service.service" ]; then
        service_file="$dir/user/$service.service"
        user_service=true
    elif [ -f "$dir/system/$service" ]; then
        service_file="$dir/system/$service"
        user_service=false
    elif [ -f "$dir/system/$service.service" ]; then
        service_file="$dir/system/$service.service"
        user_service=false
    else
        echo "The service '$service' doesn't exist"
        continue
    fi

    service_file=$(realpath "$service_file")
    service_name=$(basename "$service_file")
    service_name="${service_name%.*}"

    echo "service file: $service_file"
    echo "service name: $service_name"

    if $user_service; then
        echo "linking $HOME/.config/systemd/user/$service_name.service"
        mkdir -p "$HOME/.config/systemd/user"
        systemctl --user stop "$service_name.service" 2>/dev/null || true
        ln -f -s "$service_file" "$HOME/.config/systemd/user/$service_name.service"
        systemctl --user daemon-reload
        systemctl --user enable "$service_name.service"
        systemctl --user start "$service_name.service"
        echo "Service $service_name enabled and started in user space."
    else
        echo "linking /etc/systemd/system/$service_name.service"
        sudo systemctl stop "$service_name.service" 2>/dev/null || true
        sudo ln -f -s "$service_file" "/etc/systemd/system/$service_name.service"
        sudo systemctl daemon-reload
        sudo systemctl enable "$service_name.service"
        sudo systemctl start "$service_name.service"
        echo "Service $service_name enabled and started."
    fi

    echo
done
