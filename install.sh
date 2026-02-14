#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

print_help() {
  cat <<'USAGE'
Usage: ./install.sh [--all] [component ...] [--uninstall] [--verbose]

Components:
  packages           setup/install-packages.sh
  sudo               setup/configure-sudo.sh
  screen-lock        setup/disable-screen-lock.sh
  auto-updates       setup/disable-auto-updates.sh
  shells             setup/setup-shell-configs.sh
  editors            setup/setup-editor-configs.sh
  alacritty          setup/setup-alacritty-config.sh
  vscode             setup/setup-vscode-config.sh
  chromium           setup/setup-chromium-config.sh
  gdb                setup/setup-gdb-config.sh
  firefox-userchrome setup/setup-firefox-userchrome.sh
  desktop            setup/setup-desktop-configs.sh
USAGE
}

ALL=false
UNINSTALL=false
VERBOSE=false
COMPONENTS=()

for arg in "$@"; do
  case "$arg" in
    --all) ALL=true ;;
    --uninstall) UNINSTALL=true ;;
    --verbose) VERBOSE=true ;;
    -h|--help|help) print_help; exit 0 ;;
    *) COMPONENTS+=("$arg") ;;
  esac
done

if [[ "$ALL" == "true" || ${#COMPONENTS[@]} -eq 0 ]]; then
  COMPONENTS=(packages sudo screen-lock auto-updates shells editors alacritty vscode chromium gdb firefox-userchrome desktop)
fi

EXTRA_ARGS=()
[[ "$UNINSTALL" == "true" ]] && EXTRA_ARGS+=(--uninstall)
[[ "$VERBOSE" == "true" ]] && EXTRA_ARGS+=(--verbose)

for component in "${COMPONENTS[@]}"; do
  case "$component" in
    packages) script="setup/install-packages.sh" ;;
    sudo) script="setup/configure-sudo.sh" ;;
    screen-lock) script="setup/disable-screen-lock.sh" ;;
    auto-updates) script="setup/disable-auto-updates.sh" ;;
    shells) script="setup/setup-shell-configs.sh" ;;
    editors) script="setup/setup-editor-configs.sh" ;;
    alacritty) script="setup/setup-alacritty-config.sh" ;;
    vscode) script="setup/setup-vscode-config.sh" ;;
    chromium) script="setup/setup-chromium-config.sh" ;;
    gdb) script="setup/setup-gdb-config.sh" ;;
    firefox-userchrome) script="setup/setup-firefox-userchrome.sh" ;;
    desktop) script="setup/setup-desktop-configs.sh" ;;
    *)
      echo "Unknown component: $component" >&2
      print_help
      exit 1
      ;;
  esac

  echo "Running $script"
  "$ROOT_DIR/$script" "${EXTRA_ARGS[@]}"
  echo

done
