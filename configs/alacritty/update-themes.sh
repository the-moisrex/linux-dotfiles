#!/usr/bin/env bash

show_help() {
    cat <<'EOF'
Usage: update-themes.sh

Fetch the latest Alacritty themes from the upstream repository.

This script clones the alacritty-theme repository and performs a sparse
checkout to get only the theme files (excluding images).

Options:
  -h, --help    Show this help message
EOF
}

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    show_help
    exit 0
fi

set -euo pipefail

curdir=$(dirname "$0" | xargs realpath)
theme_dir="$curdir/alacritty-theme"

rm -rf "$theme_dir"

git clone --depth 1 --no-checkout https://github.com/alacritty/alacritty-theme "$theme_dir" >/dev/null 2>&1
builtin cd "$theme_dir" || exit 1
git config core.sparseCheckout true >/dev/null 2>&1
echo -e "/*\n!/images/" > .git/info/sparse-checkout
git checkout >/dev/null 2>&1