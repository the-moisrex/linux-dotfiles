#!/bin/bash

curdir=$(dirname "$0" | xargs realpath);
theme_dir="$curdir/alacritty-theme"

rm -rf "$theme_dir";
git clone --depth 1 --no-checkout https://github.com/alacritty/alacritty-theme "$theme_dir"
builtin cd "$theme_dir" || exit;
git config core.sparseCheckout true
echo -e "/*\n!/images/" > .git/info/sparse-checkout
git checkout
