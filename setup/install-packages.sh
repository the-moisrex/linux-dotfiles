#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
# shellcheck source=setup/lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"
SHOW_HELP=false
parse_common_flags "$@"

if [[ "$SHOW_HELP" == "true" ]]; then
  cat <<'USAGE'
Usage: ./setup/install-packages.sh [flags...]

Install or remove packages from distro-specific lists in pkgs/.

Supports Arch/Manjaro (pacman), Fedora/RHEL (dnf) and Debian/Ubuntu (apt);
apt hosts map pacman-core.txt through pkgs/core-map.txt. NONE placeholders
(not packaged on this distro) are reported and skipped, never silent.

Options:
  --offline    Skip downloading/updating packages.
  --uninstall  Remove listed packages instead of installing them.
  --verbose    Show extra debug output.
  -h, --help   Show this help message.
USAGE
    exit 0
fi

log "Managing package installation"
if $OFFLINE; then
    warn_step "Downloading packages is ignored."
    exit 0
fi

if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    DISTRO_ID=${ID}
else
    DISTRO_ID="unknown"
fi
log_step "Detected distribution: $DISTRO_ID"

PACKAGE_FILE=""
MAP_FILE=""
UPDATE_CMD=()
ACTION_CMD=()

case "$DISTRO_ID" in
    arch|manjaro)
        PACKAGE_FILE="$ROOT_DIR/pkgs/pacman-core.txt"
        UPDATE_CMD=(sudo pacman -Sy)
        if [[ "$UNINSTALL" == "true" ]]; then
            ACTION_CMD=(sudo pacman -Rns --noconfirm)
        else
            ACTION_CMD=(sudo pacman -S --noconfirm --needed)
        fi
    ;;
    fedora|rhel|centos|rocky|almalinux)
        PACKAGE_FILE="$ROOT_DIR/pkgs/dnf-core.txt"
        UPDATE_CMD=(sudo dnf makecache)
        if [[ "$UNINSTALL" == "true" ]]; then
            ACTION_CMD=(sudo dnf remove -y)
        else
            ACTION_CMD=(sudo dnf install -y)
        fi
    ;;
    debian|ubuntu|linuxmint|pop|raspbian|elementary|zorin|kali|neon)
        PACKAGE_FILE="$ROOT_DIR/pkgs/pacman-core.txt"
        MAP_FILE="$ROOT_DIR/pkgs/core-map.txt"
        UPDATE_CMD=(sudo apt-get update)
        if [[ "$UNINSTALL" == "true" ]]; then
            ACTION_CMD=(sudo apt-get remove -y)
        else
            ACTION_CMD=(sudo apt-get install -y)
        fi
    ;;
    *)
        warn_step "No supported package list for distribution: $DISTRO_ID"
        exit 0
        ;;
esac

if [[ ! -f "$PACKAGE_FILE" ]]; then
    warn "Package file does not exist: $PACKAGE_FILE"
    exit 0
fi

declare -A MAP=()
if [[ -n "$MAP_FILE" ]]; then
    if [[ ! -f "$MAP_FILE" ]]; then
        warn "Package map does not exist: $MAP_FILE"
        exit 0
    fi
    while read -r col1 col2 _rest; do
        if [[ -z "$col1" || "$col1" == '#'* ]]; then continue; fi
        if [[ -n "$col2" ]]; then MAP["$col1"]="$col2"; fi
    done < "$MAP_FILE"
    log_step "Using package map: $MAP_FILE (debian/ubuntu column)"
fi

log_step "Using package list: $PACKAGE_FILE"
run_cmd "${UPDATE_CMD[@]}"

while IFS= read -r package || [[ -n "$package" ]]; do
    package="${package%%#*}"
    package=$(echo "$package" | xargs)
    [[ -z "$package" ]] && continue
    case "$package" in
        '$'*)
            warn_step "Special entry (manual install): $package"
            continue
        ;;
    esac
    target="$package"
    if [[ -n "$MAP_FILE" ]]; then
        v="${MAP[$package]:-}"
        if [[ -z "$v" ]]; then
            warn_step "No core-map.txt entry for $package; skipping"
            continue
        fi
        case "$v" in
            NONE)
                warn_step "Not packaged on $DISTRO_ID (NONE placeholder): $package"
                continue
            ;;
            '$'*)
                warn_step "Special entry (manual install): $package"
                continue
            ;;
        esac
        target="$v"
        if ! apt-cache show "$target" >/dev/null 2>&1; then
            warn_step "Not packaged on $DISTRO_ID (not in repositories): $package"
            continue
        fi
    fi
    if [[ "$UNINSTALL" == "true" ]]; then
        action="Removing"
    else
        action="Installing"
    fi
    if [[ "$target" == "$package" ]]; then
        log_step "$action package: $package"
    else
        log_step "$action package: $package ($target)"
    fi
    run_cmd_may_fail "${ACTION_CMD[@]}" "$target"
done < "$PACKAGE_FILE"

log "Done"
