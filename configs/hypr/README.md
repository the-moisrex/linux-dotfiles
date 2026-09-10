# Hyprland Configuration

Personal Hyprland config, managed as symlinks from `~/.config/hypr/`.

## Files

| File | Tracked | Description |
|------|---------|-------------|
| `hyprland.lua` | Yes | Main config (monitors, input, keybinds, window rules) |
| `monitors.lua` | Yes | Auto-detect monitors from xrandr |
| `local.lua` | No | **Machine-specific** — any local overrides |

## Setup

```bash
./install.sh hyprland
```

Or manually:

```bash
ln -s ~/cmd/configs/hypr ~/.config/hypr
```

## Monitor Configuration

Monitors are **auto-detected** from `xrandr --listmonitors` — no manual config needed. They're arranged left-to-right by x-position.

If auto-detection fails, edit `monitors.lua` with manual entries:

```lua
hl.monitor({ output = "DP-1", mode = "2560x1440@144", position = "1600x0", scale = 1 })
hl.monitor({ output = "HDMI-A-2", mode = "1600x900@60", position = "0x270", scale = 1 })
```

Find your monitor info:

```bash
xrandr --listmonitors    # monitor names and positions
xrandr                   # available modes and refresh rates
```

## Keybindings

| Key | Action |
|-----|--------|
| `SUPER + Q` | Terminal (kitty) |
| `SUPER + Space` | Launcher (Noctalia) |
| `SUPER + E` | File manager (dolphin) |
| `SUPER + C` | Close window |
| `SUPER + grave` | Control center |
| `SUPER + comma` | Settings |
| `ALT + Tab` | Window switcher |
| `PRINT` | Screenshot region |
| `SUPER + [1-9]` | Switch to workspace |
| `SUPER + Shift + [1-9]` | Move window to workspace |
| `SUPER + Arrow keys` | Move focus |
| `SUPER + V` | Toggle floating |
| `SUPER + J` | Toggle split |
| `SUPER + S` | Toggle scratchpad |

## Uninstall

```bash
./install.sh --uninstall hyprland
```
