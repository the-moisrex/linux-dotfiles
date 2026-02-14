# Setup Scripts

Modular scripts for configuring this Linux dotfiles repository.

## Scripts

- `configure-sudo.sh`: Configure/remove passwordless sudo for the current user.
- `install-packages.sh`: Install/remove packages from distro-specific lists in `pkgs/`.
- `disable-screen-lock.sh`: Disable/restore lock and display sleep settings.
- `disable-auto-updates.sh`: Disable/re-enable automatic update mechanisms.
- `setup-shell-configs.sh`: Install/remove Fish and Nushell configs.
- `setup-editor-configs.sh`: Install/remove editor configs (Neovim and SpaceVim).
- `setup-alacritty-config.sh`: Install/remove Alacritty config.
- `setup-vscode-config.sh`: Install/remove VS Code settings.
- `setup-chromium-config.sh`: Install/remove Chromium flags config.
- `setup-gdb-config.sh`: Install/remove GDB config.
- `setup-firefox-userchrome.sh`: Install/remove Firefox `userChrome.css` in a profile.
- `setup-desktop-configs.sh`: Install/remove desktop integration configs.

## Common flags

Every setup script supports:

- `--uninstall`: Revert/remove what the script configures.
- `--verbose`: Show extra debug and command-level logs.
- `--help`: Show usage.

## Logging format

- Top-level status lines have no indentation.
- Sub-steps are prefixed by two spaces for readable output, especially when chaining scripts (for example: `./setup/*.sh`).
- Verbose-only details are prefixed by four spaces.

## Orchestration

Use `./install.sh` as a wrapper for these scripts:

```bash
./install.sh --all --verbose
./install.sh packages shells
./install.sh --uninstall editors desktop
```
