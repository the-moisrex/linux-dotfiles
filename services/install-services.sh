#!/bin/bash

dir=$(dirname "$0")
services="$@"

for service in $services; do

    service_file="$service";
    user_service=false

    if [ -f $service_file ]; then
        # nothing
        user_service=false;
        service_file="$service";
    elif [ -f "$dir/user/$service" ]; then
        service_file="$dir/user/$service";
        user_service=true;
    elif [ -f "$dir/user/$service.service" ]; then
        service_file="$dir/user/$service.service";
        user_service=true;
    elif [ -f "$dir/system/$service" ]; then
        service_file="$dir/system/$service";
        user_service=false;
    elif [ -f "$dir/system/$service.service" ]; then
        service_file="$dir/system/$service.service";
        user_service=false;
    else
        echo "The service '$service' doesn't exists";
        continue;
    fi

    service_file=$(realpath "$service_file");
    service_name=$(basename "$service_file");
    service_name="${service_name%.*}";

    echo service file: $service_file;
    echo service name: $service_name;
    echo

    if $user_service; then
        mkdir -p "$HOME/.config/systemd/user";
        ln -f -s "$service_file" "$HOME/.config/systemd/user/$service_name.service";
        systemctl --user daemon-reload;
        systemctl --user enable $service_name.service;
        systemctl --user start $service_name.service;
    else
        sudo ln -f -s "$service_file" "/etc/systemd/system/$service_name.service";
        sudo systemctl daemon-reload;
        sudo systemctl enable $service_name.service;
        sudo systemctl start $service_name.service;
    fi

done
