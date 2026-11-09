-- Hyprland Lua config — Niri-style scrolling layout with dual independent 5x2 grid desktops
--
-- HDMI-A-2:  1  2  3  4  5       DP-1:  11 12 13 14 15
--            6  7  8  9 10              16 17 18 19 20
--
-- Left/Right: focus the next scrolling window; at edge, switch to adjacent workspace in the SAME row.
-- Up/Down:    switch to the corresponding workspace in the other row (vertical grid jump).
-- Shift+arrows/HJKL: move the active window between adjacent workspaces on the grid.

------------------
---- MONITORS ----
------------------

hl.monitor({
    output   = "HDMI-A-2",
    mode     = "preferred",
    position = "0x0",
    scale    = 1,
})

hl.monitor({
    output   = "DP-1",
    mode     = "2560x1440@74.971",
    position = "1600x0",
    scale    = 1,
})

-- Fallback monitor
hl.monitor({
    output   = "",
    mode     = "preferred",
    position = "auto",
    scale    = "auto",
})


-------------------------------
---- ENVIRONMENT VARIABLES ----
-------------------------------

hl.env("XCURSOR_SIZE", "24")
hl.env("HYPRCURSOR_SIZE", "24")


-------------------
---- AUTOSTART ----
-------------------

hl.on("hyprland.start", function()
    hl.exec_cmd("systemctl --user start hyprpolkitagent")
    hl.exec_cmd("noctalia")
    hl.exec_cmd("hyprpaper")
    hl.exec_cmd("nm-applet")
    hl.exec_cmd("mkdir -p \"$HOME/Pictures/Screenshots\"")
end)


-----------------------------------------
---- PER-MONITOR WORKSPACES & RULES ----
-----------------------------------------

local WS_PER_MON   = 10
local COLS_PER_ROW = 5

-- HDMI-A-2 → 1..10
for i = 1, WS_PER_MON do
    hl.workspace_rule({
        workspace  = tostring(i),
        monitor    = "HDMI-A-2",
        default    = (i == 1),
        persistent = true,
        layout     = "scrolling",
    })
end

-- DP-1 → 11..20
for i = 1, WS_PER_MON do
    hl.workspace_rule({
        workspace  = tostring(WS_PER_MON + i),
        monitor    = "DP-1",
        default    = (i == 1),
        persistent = true,
        layout     = "scrolling",
    })
end


--------------------------------
---- GRID NAVIGATION LOGIC -----
--------------------------------

-- Return active monitor workspace base offset (HDMI-A-2 -> 0, DP-1 -> 10)
local function monitor_base()
    local mon = hl.get_active_monitor()
    if not mon then return nil end

    if mon.name == "HDMI-A-2" then return 0 end
    if mon.name == "DP-1" then return WS_PER_MON end

    return nil
end

-- Return active workspace local index (1..10) and base offset
local function grid_position()
    local base = monitor_base()
    local ws = hl.get_active_workspace()

    if base == nil or not ws then return nil, nil end

    local index = ws.id - base
    if index < 1 or index > WS_PER_MON then
        return nil, nil
    end

    return index, base
end

local function same_row(a, b)
    return math.floor((a - 1) / COLS_PER_ROW) == math.floor((b - 1) / COLS_PER_ROW)
end

-- Switch workspace without leaving the current monitor's grid
local function focus_grid(delta, horizontal)
    local index, base = grid_position()
    if not index then return end

    local next_index = index + delta
    if next_index < 1 or next_index > WS_PER_MON then return end
    if horizontal and not same_row(index, next_index) then return end

    hl.dispatch(hl.dsp.focus({
        workspace = tostring(base + next_index),
    }))
end

-- Move active window to another workspace and follow focus
local function move_window_grid(delta, horizontal)
    local index, base = grid_position()
    if not index or not hl.get_active_window() then return end

    local next_index = index + delta
    if next_index < 1 or next_index > WS_PER_MON then return end
    if horizontal and not same_row(index, next_index) then return end

    hl.dispatch(hl.dsp.window.move({
        workspace = tostring(base + next_index),
        follow    = true,
    }))
end

-- Left/Right focuses next window on scrolling tape; at edge, moves to adjacent workspace in the same row
local function focus_or_workspace(direction, delta)
    return function()
        local index, base = grid_position()
        if not index then return end

        local before = hl.get_active_window()
        local before_address = before and before.address

        hl.dispatch(hl.dsp.layout("focus " .. direction))

        local current_index, current_base = grid_position()
        if current_index ~= index or current_base ~= base then return end

        local after = hl.get_active_window()
        local after_address = after and after.address

        -- Edge reached
        if before_address == after_address then
            focus_grid(delta, true)
        end
    end
end

local function focus_row(delta)
    return function()
        focus_grid(delta * COLS_PER_ROW, false)
    end
end

local function move_row(delta)
    return function()
        move_window_grid(delta * COLS_PER_ROW, false)
    end
end

local function move_column(delta)
    return function()
        move_window_grid(delta, true)
    end
end

-- Direct workspace jump by local index 1..10
local function focus_local(n)
    return function()
        local base = monitor_base()
        if base ~= nil then
            hl.dispatch(hl.dsp.focus({ workspace = tostring(base + n) }))
        end
    end
end

local function move_local(n)
    return function()
        local base = monitor_base()
        if base ~= nil then
            hl.dispatch(hl.dsp.window.move({
                workspace = tostring(base + n),
                follow    = true,
            }))
        end
    end
end


-------------------------------
---- CONFIG & LOOK-AND-FEEL ---
-------------------------------

hl.config({
    render = {
        direct_scanout = 0,
    },

    input = {
        kb_layout          = "us,ir",
        kb_options         = "grp:win_space_toggle",
        numlock_by_default = true,
        follow_mouse       = 1,
        sensitivity        = 0,

        touchpad = {
            tap_to_click   = true,
            natural_scroll = true,
        },
    },

    general = {
        gaps_in           = 8,
        gaps_out          = 8,
        border_size       = 2,
        col = {
            active_border   = "rgba(f38ba8ff)",
            inactive_border = "rgba(45475aff)",
        },
        layout            = "scrolling",
        resize_on_border  = false,
        allow_tearing     = false,
        no_focus_fallback = true,
    },

    decoration = {
        rounding         = 0,
        active_opacity   = 1.0,
        inactive_opacity = 1.0,

        blur = {
            enabled = true,
            size    = 3,
            passes  = 1,
        },

        shadow = {
            enabled      = true,
            range        = 4,
            render_power = 3,
            color        = "rgba(1a1a1aee)",
        },
    },

    scrolling = {
        direction                = "right",
        column_width             = 0.5,
        fullscreen_on_one_column = true,
        follow_focus             = true,
        follow_min_visible       = 0.4,
        focus_fit_method         = 1,
        wrap_focus               = false, -- Required to trigger edge detection for workspace change
        wrap_swapcol             = true,
        explicit_column_widths   = "0.333, 0.5, 0.667, 1.0",
    },

    binds = {
        window_direction_monitor_fallback = false,
        workspace_back_and_forth          = true,
    },

    misc = {
        force_default_wallpaper = -1,
        disable_hyprland_logo   = true,
        vrr                     = 0,
    },
})

hl.gesture({
    fingers   = 3,
    direction = "horizontal",
    action    = "workspace",
})


--------------------
---- ANIMATIONS ----
--------------------

hl.curve("fast", {
    type   = "bezier",
    points = {
        { 0.41, 0.6 },
        { 0.98, 0.93 },
    },
})

hl.config({
    animations = {
        enabled = true,
    },
})

hl.animation({
    leaf    = "global",
    enabled = true,
    speed   = 3,
    bezier  = "default",
})

hl.animation({
    leaf    = "windows",
    enabled = true,
    speed   = 1,
    bezier  = "fast",
    style   = "slide",
})

hl.animation({
    leaf    = "windowsMove",
    enabled = true,
    speed   = 1,
    bezier  = "fast",
    style   = "slide",
})

hl.animation({
    leaf    = "workspaces",
    enabled = true,
    speed   = 1,
    bezier  = "fast",
    style   = "slide",
})

hl.animation({
    leaf    = "border",
    enabled = true,
    speed   = 1,
    bezier  = "default",
})

hl.animation({
    leaf    = "fade",
    enabled = true,
    speed   = 1,
    bezier  = "default",
})

hl.animation({
    leaf    = "specialWorkspace",
    enabled = false,
})


--------------------------------
---- WINDOWS AND WORKSPACES ----
--------------------------------

hl.window_rule({
    name  = "firefox-pip-float",
    match = { class = "firefox", title = "^Picture-in-Picture$" },
    float = true,
    pin   = true,
})

hl.window_rule({
    name  = "noctalia-float",
    match = { class = "dev.noctalia.Noctalia" },
    float = true,
})

hl.window_rule({
    name           = "suppress-maximize-events",
    match          = { class = ".*" },
    suppress_event = "maximize",
})

hl.window_rule({
    name  = "fix-xwayland-drags",
    match = {
        class      = "^$",
        title      = "^$",
        xwayland   = true,
        float      = true,
        fullscreen = false,
        pin        = false,
    },
    no_focus = true,
})


---------------------
---- KEYBINDINGS ----
---------------------

local mainMod = "SUPER"

-- Programs
hl.bind(mainMod .. " + Return",       hl.dsp.exec_cmd("kitty"),
    { description = "Terminal" })
hl.bind("CTRL + SHIFT + Return",      hl.dsp.exec_cmd("firefox-developer-edition --new-window"),
    { description = "Firefox (new window)" })
hl.bind(mainMod .. " + Pause",        hl.dsp.exec_cmd("sh -c \"$HOME/cmd/bin/sig-gui.py\""),
    { description = "Signal GUI" })
hl.bind(mainMod .. " + ALT + Return", hl.dsp.exec_cmd("gnome-text-editor --new-window"),
    { description = "Text editor" })
hl.bind(mainMod .. " + E",            hl.dsp.exec_cmd("dolphin"),
    { description = "File manager" })

-- Noctalia UI
hl.bind(mainMod .. " + grave",          hl.dsp.exec_cmd("noctalia msg panel-toggle control-center"))
hl.bind(mainMod .. " + SHIFT + period", hl.dsp.exec_cmd("noctalia msg settings-toggle"))
hl.bind(mainMod .. " + D",              hl.dsp.exec_cmd("noctalia msg panel-toggle launcher"),
    { description = "Launcher" })

-- Window actions
hl.bind(mainMod .. " + Q", hl.dsp.window.close(),
    { description = "Close window" })
hl.bind(mainMod .. " + V", function()
    hl.dispatch(hl.dsp.window.float({ action = "toggle" }))
end, { description = "Toggle float" })
hl.bind(mainMod .. " + S", hl.dsp.window.pseudo(),
    { description = "Toggle pseudo" })

-- ── Grid & Column Focus (Arrow Keys & HJKL) ───────────────
hl.bind(mainMod .. " + left",  focus_or_workspace("l", -1))
hl.bind(mainMod .. " + right", focus_or_workspace("r",  1))
hl.bind(mainMod .. " + up",    focus_row(-1))
hl.bind(mainMod .. " + down",  focus_row( 1))

hl.bind(mainMod .. " + H",     focus_or_workspace("l", -1))
hl.bind(mainMod .. " + L",     focus_or_workspace("r",  1))
hl.bind(mainMod .. " + K",     focus_row(-1))
hl.bind(mainMod .. " + J",     focus_row( 1))

-- Focus vertically within a single column
hl.bind("ALT + up",   hl.dsp.focus({ direction = "u" }))
hl.bind("ALT + down", hl.dsp.focus({ direction = "d" }))
hl.bind("ALT + K",    hl.dsp.focus({ direction = "u" }))
hl.bind("ALT + J",    hl.dsp.focus({ direction = "d" }))

-- ── Move Active Window Across Grid ────────────────────────
hl.bind(mainMod .. " + SHIFT + left",  move_column(-1))
hl.bind(mainMod .. " + SHIFT + right", move_column( 1))
hl.bind(mainMod .. " + SHIFT + up",    move_row(-1))
hl.bind(mainMod .. " + SHIFT + down",  move_row( 1))

-- ── Move Windows Within Workspace (Scrolling Native) ──────
hl.bind(mainMod .. " + CTRL + left",  hl.dsp.layout("swapcol l"))
hl.bind(mainMod .. " + CTRL + right", hl.dsp.layout("swapcol r"))
hl.bind(mainMod .. " + CTRL + up",    hl.dsp.window.move({ direction = "u" }))
hl.bind(mainMod .. " + CTRL + down",  hl.dsp.window.move({ direction = "d" }))

hl.bind(mainMod .. " + CTRL + H",     hl.dsp.layout("swapcol l"))
hl.bind(mainMod .. " + CTRL + L",     hl.dsp.layout("swapcol r"))
hl.bind(mainMod .. " + CTRL + K",     hl.dsp.window.move({ direction = "u" }))
hl.bind(mainMod .. " + CTRL + J",     hl.dsp.window.move({ direction = "d" }))

-- ── Monitor Focus & Monitor Moving ────────────────────────
hl.bind(mainMod .. " + SHIFT + H", hl.dsp.focus({ monitor = "l" }))
hl.bind(mainMod .. " + SHIFT + L", hl.dsp.focus({ monitor = "r" }))

hl.bind(mainMod .. " + SHIFT + CTRL + left",  hl.dsp.window.move({ monitor = "l" }))
hl.bind(mainMod .. " + SHIFT + CTRL + right", hl.dsp.window.move({ monitor = "r" }))
hl.bind(mainMod .. " + SHIFT + CTRL + up",    hl.dsp.window.move({ monitor = "u" }))
hl.bind(mainMod .. " + SHIFT + CTRL + down",  hl.dsp.window.move({ monitor = "d" }))

hl.bind(mainMod .. " + SHIFT + CTRL + H", hl.dsp.window.move({ monitor = "l" }))
hl.bind(mainMod .. " + SHIFT + CTRL + L", hl.dsp.window.move({ monitor = "r" }))
hl.bind(mainMod .. " + SHIFT + CTRL + K", hl.dsp.window.move({ monitor = "u" }))
hl.bind(mainMod .. " + SHIFT + CTRL + J", hl.dsp.window.move({ monitor = "d" }))

-- ── Direct Workspace Numbers (1–9, 0 = 10) ────────────────
for i = 1, 9 do
    hl.bind(mainMod .. " + " .. i,        focus_local(i))
    hl.bind(mainMod .. " + CTRL + " .. i, move_local(i))
end
hl.bind(mainMod .. " + 0",        focus_local(10))
hl.bind(mainMod .. " + CTRL + 0", move_local(10))

-- Scratchpad
hl.bind(mainMod .. " + SHIFT + S", hl.dsp.workspace.toggle_special("scratch"),
    { description = "Toggle scratchpad" })
hl.bind(mainMod .. " + ALT + S",   hl.dsp.window.move({ workspace = "special:scratch" }),
    { description = "Move to scratchpad" })

-- Mouse wheel navigation
hl.bind(mainMod .. " + mouse_down",        move_column(1))
hl.bind(mainMod .. " + mouse_up",          move_column(-1))
hl.bind(mainMod .. " + CTRL + mouse_down", move_row(1))
hl.bind(mainMod .. " + CTRL + mouse_up",   move_row(-1))

-- Column resize (scrolling-native)
hl.bind(mainMod .. " + bracketleft",  hl.dsp.layout("colresize -conf"))
hl.bind(mainMod .. " + bracketright", hl.dsp.layout("colresize +conf"))

hl.bind(mainMod .. " + minus",         hl.dsp.layout("colresize -0.1"))
hl.bind(mainMod .. " + equal",         hl.dsp.layout("colresize +0.1"))
hl.bind(mainMod .. " + SHIFT + minus", hl.dsp.window.resize({ x = 0, y = -40, relative = true }))
hl.bind(mainMod .. " + SHIFT + equal", hl.dsp.window.resize({ x = 0, y =  40, relative = true }))

-- Maximize / fullscreen / center
hl.bind(mainMod .. " + F",         hl.dsp.window.fullscreen({ mode = "maximized" }),
    { description = "Maximize" })
hl.bind(mainMod .. " + SHIFT + F", hl.dsp.window.fullscreen({ mode = "fullscreen" }),
    { description = "Fullscreen" })
hl.bind(mainMod .. " + M",         hl.dsp.window.fullscreen({ mode = "maximized" }))
hl.bind(mainMod .. " + C",         hl.dsp.window.center({}),
    { description = "Center window" })

-- Screenshots
local function shotName()
    return "\"$HOME/Pictures/Screenshots/Screenshot from $(date +'%Y-%m-%d %H-%M-%S').png\""
end

hl.bind("Print", hl.dsp.exec_cmd(
    "grim -g \"$(slurp)\" " .. shotName()
), { description = "Screenshot region" })

hl.bind("CTRL + Print", hl.dsp.exec_cmd(
    "grim " ..  shotName()
), { description = "Screenshot full" })

hl.bind("ALT + Print", hl.dsp.exec_cmd(
    "grim -g \"$(hyprctl activewindow -j | jq -r '\"\\(.at[0]),\\(.at[1]) \\(.size[0])x\\(.size[1])\"')\" "
    .. shotName()
), { description = "Screenshot active window" })

hl.bind(mainMod .. " + Print", hl.dsp.exec_cmd("noctalia msg screenshot-annotate"),
    { description = "Annotate screenshot" })

-- Session
hl.bind(mainMod .. " + SHIFT + E", hl.dsp.exec_cmd(
    "command -v hyprshutdown >/dev/null 2>&1 && hyprshutdown || hyprctl dispatch exit"
), { description = "Exit Hyprland" })

hl.bind("CTRL + ALT + Delete", hl.dsp.exec_cmd(
    "command -v hyprshutdown >/dev/null 2>&1 && hyprshutdown || hyprctl dispatch exit"
))

hl.bind(mainMod .. " + SHIFT + P", function()
    hl.timer(function()
        hl.dispatch(hl.dsp.dpms({ action = "off" }))
    end, { timeout = 300, type = "oneshot" })
end, { description = "Turn off displays" })

-- Media / Brightness
local mediaFlags = { locked = true }

hl.bind("XF86AudioRaiseVolume",  hl.dsp.exec_cmd("noctalia msg volume-up"),        mediaFlags)
hl.bind("XF86AudioLowerVolume",  hl.dsp.exec_cmd("noctalia msg volume-down"),      mediaFlags)
hl.bind("XF86AudioMute",         hl.dsp.exec_cmd("noctalia msg volume-mute"),      mediaFlags)
hl.bind("XF86AudioMicMute",      hl.dsp.exec_cmd("noctalia msg mic-mute"),         mediaFlags)
hl.bind("XF86AudioPlay",         hl.dsp.exec_cmd("noctalia msg media play-pause"), mediaFlags)
hl.bind("XF86AudioPause",        hl.dsp.exec_cmd("noctalia msg media play-pause"), mediaFlags)
hl.bind("XF86AudioStop",         hl.dsp.exec_cmd("noctalia msg media stop"),       mediaFlags)
hl.bind("XF86AudioPrev",         hl.dsp.exec_cmd("noctalia msg media previous"),   mediaFlags)
hl.bind("XF86AudioNext",         hl.dsp.exec_cmd("noctalia msg media next"),       mediaFlags)
hl.bind("XF86MonBrightnessUp",   hl.dsp.exec_cmd("noctalia msg brightness-up"),    mediaFlags)
hl.bind("XF86MonBrightnessDown", hl.dsp.exec_cmd("noctalia msg brightness-down"),  mediaFlags)

-- Mouse drag / resize
hl.bind(mainMod .. " + mouse:272", hl.dsp.window.drag(),
    { mouse = true })
hl.bind(mainMod .. " + mouse:273", hl.dsp.window.resize({ keep_aspect_ratio = false }),
    { mouse = true })

