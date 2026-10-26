-- Hyprland Lua config — Niri-style scrolling layout with dual independent desktops
-- HDMI-A-2: workspaces 1–10  |  DP-1: workspaces 11–20

------------------
---- MONITORS ----
------------------

hl.monitor({
    output   = "HDMI-A-2",
    mode     = "1600x900",
    position = "0x0",
    scale    = 1,
})

hl.monitor({
    output   = "DP-1",
    mode     = "2560x1440@74.971",
    position = "1600x0",
    scale    = 1,
})

hl.monitor({
    output   = "",
    mode     = "preferred",
    position = "auto",
    scale    = "auto",
})


-----------------------------------------
---- PER-MONITOR WORKSPACES (niri) ----
-----------------------------------------

local WS_PER_MON = 10

-- HDMI-A-2 → 1..10
for i = 1, WS_PER_MON do
    hl.workspace_rule({
        workspace  = tostring(i),
        monitor    = "HDMI-A-2",
        default    = (i == 1),
        persistent = true,
    })
end

-- DP-1 → 11..20
for i = 1, WS_PER_MON do
    hl.workspace_rule({
        workspace  = tostring(WS_PER_MON + i),
        monitor    = "DP-1",
        default    = (i == 1),
        persistent = true,
    })
end

-- Map monitor name → base offset (0 for first, 10 for second)
local function mon_base()
    local mon = hl.get_active_monitor()
    if not mon then return 0 end
    if mon.name == "DP-1" then return WS_PER_MON end
    return 0 -- HDMI-A-2 and anything else
end

local function local_ws_id(n)
    return mon_base() + n
end

local function focus_local(n)
    return function()
        hl.dispatch(hl.dsp.focus({ workspace = local_ws_id(n) }))
    end
end

local function move_local(n)
    return function()
        hl.dispatch(hl.dsp.window.move({
            workspace = local_ws_id(n),
            follow    = true,
        }))
    end
end

-- Relative local workspace (no wrap)
local function focus_local_rel(delta)
    return function()
        local ws = hl.get_active_workspace()
        if not ws then return end
        local base  = mon_base()
        local local_i = ws.id - base
        if local_i < 1 then local_i = 1 end
        if local_i > WS_PER_MON then local_i = WS_PER_MON end
        local next_i = local_i + delta
        if next_i < 1 or next_i > WS_PER_MON then return end -- no wrap
        hl.dispatch(hl.dsp.focus({ workspace = base + next_i }))
    end
end

local function move_local_rel(delta)
    return function()
        local ws = hl.get_active_workspace()
        if not ws then return end
        local base  = mon_base()
        local local_i = ws.id - base
        if local_i < 1 then local_i = 1 end
        if local_i > WS_PER_MON then local_i = WS_PER_MON end
        local next_i = local_i + delta
        if next_i < 1 or next_i > WS_PER_MON then return end
        hl.dispatch(hl.dsp.window.move({
            workspace = base + next_i,
            follow    = true,
        }))
    end
end

-- Focus window/column in direction; if at edge → prev/next local workspace
local function focus_or_workspace(dir, ws_delta)
    return function()
        local before = hl.get_active_window()
        local addr   = before and before.address

        if dir == "l" or dir == "r" then
            hl.dispatch(hl.dsp.layout("focus " .. dir))
        else
            hl.dispatch(hl.dsp.focus({ direction = dir }))
        end

        local after = hl.get_active_window()
        local addr2 = after and after.address

        if addr == addr2 then
            local ws = hl.get_active_workspace()
            if not ws then return end
            local base    = mon_base()
            local local_i = ws.id - base
            if local_i < 1 then local_i = 1 end
            if local_i > WS_PER_MON then local_i = WS_PER_MON end
            local next_i = local_i + ws_delta
            if next_i < 1 or next_i > WS_PER_MON then return end
            hl.dispatch(hl.dsp.focus({ workspace = base + next_i }))
        end
    end
end


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


-------------------------------
---- ENVIRONMENT VARIABLES ----
-------------------------------

hl.env("XCURSOR_SIZE", "24")
hl.env("HYPRCURSOR_SIZE", "24")


---------------
---- INPUT ----
---------------

hl.config({
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
        no_focus_fallback = true,
    },

    binds = {
        window_direction_monitor_fallback = false,
        workspace_back_and_forth          = true,
    },
})

hl.gesture({
    fingers   = 3,
    direction = "horizontal",
    action    = "workspace",
})


-----------------------
---- LOOK AND FEEL ----
-----------------------

hl.config({
    general = {
        gaps_in      = 8,
        gaps_out     = 8,
        border_size  = 2,
        col = {
            active_border   = "rgba(f38ba8ff)",
            inactive_border = "rgba(45475aff)",
        },
        layout           = "scrolling",
        resize_on_border = false,
        allow_tearing    = false,
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
        column_width             = 0.5,
        fullscreen_on_one_column = false,
        follow_focus             = true,
        focus_fit_method         = 1,
        wrap_focus               = false,
        wrap_swapcol             = false,
        direction                = "right",
    },

    misc = {
        force_default_wallpaper = -1,
        disable_hyprland_logo   = true,
    },
})


--------------------------------
---- ANIMATIONS (FASTER) ----
--------------------------------

-- Define a snappy, fast Bézier curve for responsive motion
hl.curve("fast", {
    type   = "bezier",
    points = {
        { 0.41, 0.6 },
        { 0.02, 0.93 },
    },
})

hl.config({
    animations = {
        enabled = true,
    },
})

-- Global: slightly faster than default for overall snappiness
hl.animation({
    leaf    = "global",
    enabled = true,
    speed   = 3,
    bezier  = "default", -- Changed from curve
})

-- Windows: instant feel for opening/closing
hl.animation({
    leaf    = "windows",
    enabled = true,
    speed   = 1,
    bezier  = "fast", -- Changed from curve
    style   = "slide",
})

-- windowsMove: governs layout scrolling and window movement — make it very fast
hl.animation({
    leaf    = "windowsMove",
    enabled = true,
    speed   = 1,
    bezier  = "fast", -- Changed from curve
    style   = "slide",
})

-- Workspaces: fast switching between desktops
hl.animation({
    leaf    = "workspaces",
    enabled = true,
    speed   = 1,
    bezier  = "fast", -- Changed from curve
    style   = "slide",
})

-- Borders and fades: keep consistent with the fast feel
hl.animation({
    leaf    = "border",
    enabled = true,
    speed   = 1,
    bezier  = "default", -- Changed from curve
})

hl.animation({
    leaf    = "fade",
    enabled = true,
    speed   = 1,
    bezier  = "default", -- Changed from curve
})

-- Disable the special workspace animation to avoid any perceived lag
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

-- Noctalia
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

-- ── Focus: arrows / hjkl ──────────────────────────────────
-- Left/Right: focus column, or prev/next local workspace at edge
hl.bind(mainMod .. " + left",  focus_or_workspace("l", -1))
hl.bind(mainMod .. " + right", focus_or_workspace("r",  1))
-- Up/Down: focus windows vertically within a column (moved to ALT to free SUPER for workspaces)
hl.bind("ALT + up",    hl.dsp.focus({ direction = "u" }))
hl.bind("ALT + down",  hl.dsp.focus({ direction = "d" }))

hl.bind(mainMod .. " + H", focus_or_workspace("l", -1))
hl.bind(mainMod .. " + L", focus_or_workspace("r",  1))
hl.bind("ALT + K", hl.dsp.focus({ direction = "u" }))
hl.bind("ALT + J", hl.dsp.focus({ direction = "d" }))

-- ── Move windows between columns (scrolling-native) ──────
hl.bind(mainMod .. " + CTRL + left",  hl.dsp.layout("swapcol l"))
hl.bind(mainMod .. " + CTRL + right", hl.dsp.layout("swapcol r"))
hl.bind(mainMod .. " + CTRL + up",    hl.dsp.window.move({ direction = "u" }))
hl.bind(mainMod .. " + CTRL + down",  hl.dsp.window.move({ direction = "d" }))

hl.bind(mainMod .. " + CTRL + H", hl.dsp.layout("swapcol l"))
hl.bind(mainMod .. " + CTRL + L", hl.dsp.layout("swapcol r"))
hl.bind(mainMod .. " + CTRL + K", hl.dsp.window.move({ direction = "u" }))
hl.bind(mainMod .. " + CTRL + J", hl.dsp.window.move({ direction = "d" }))

-- ── Move entire column to workspace above/below (Niri-style) ──
hl.bind(mainMod .. " + SHIFT + up",   hl.dsp.layout("movecoltoworkspace +1"))
hl.bind(mainMod .. " + SHIFT + down", hl.dsp.layout("movecoltoworkspace -1"))
hl.bind(mainMod .. " + SHIFT + K",    hl.dsp.layout("movecoltoworkspace +1"))
hl.bind(mainMod .. " + SHIFT + J",    hl.dsp.layout("movecoltoworkspace -1"))

-- Monitor focus (explicit cross-monitor)
hl.bind(mainMod .. " + SHIFT + left",  hl.dsp.focus({ monitor = "l" }))
hl.bind(mainMod .. " + SHIFT + right", hl.dsp.focus({ monitor = "r" }))
hl.bind(mainMod .. " + SHIFT + up",    hl.dsp.focus({ monitor = "u" }))
hl.bind(mainMod .. " + SHIFT + down",  hl.dsp.focus({ monitor = "d" }))

hl.bind(mainMod .. " + SHIFT + H", hl.dsp.focus({ monitor = "l" }))
hl.bind(mainMod .. " + SHIFT + L", hl.dsp.focus({ monitor = "r" }))
hl.bind(mainMod .. " + SHIFT + K", hl.dsp.focus({ monitor = "u" }))
hl.bind(mainMod .. " + SHIFT + J", hl.dsp.focus({ monitor = "d" }))

-- Move window to other monitor
hl.bind(mainMod .. " + SHIFT + CTRL + left",  hl.dsp.window.move({ monitor = "l" }))
hl.bind(mainMod .. " + SHIFT + CTRL + right", hl.dsp.window.move({ monitor = "r" }))
hl.bind(mainMod .. " + SHIFT + CTRL + up",    hl.dsp.window.move({ monitor = "u" }))
hl.bind(mainMod .. " + SHIFT + CTRL + down",  hl.dsp.window.move({ monitor = "d" }))

hl.bind(mainMod .. " + SHIFT + CTRL + H", hl.dsp.window.move({ monitor = "l" }))
hl.bind(mainMod .. " + SHIFT + CTRL + L", hl.dsp.window.move({ monitor = "r" }))
hl.bind(mainMod .. " + SHIFT + CTRL + K", hl.dsp.window.move({ monitor = "u" }))
hl.bind(mainMod .. " + SHIFT + CTRL + J", hl.dsp.window.move({ monitor = "d" }))

-- Relative local workspaces (Page/U/I)
hl.bind(mainMod .. " + Page_Down",        focus_local_rel(1))
hl.bind(mainMod .. " + Page_Up",          focus_local_rel(-1))
hl.bind(mainMod .. " + U",                focus_local_rel(1))
hl.bind(mainMod .. " + I",                focus_local_rel(-1))

hl.bind(mainMod .. " + CTRL + Page_Down", move_local_rel(1))
hl.bind(mainMod .. " + CTRL + Page_Up",   move_local_rel(-1))
hl.bind(mainMod .. " + CTRL + U",         move_local_rel(1))
hl.bind(mainMod .. " + CTRL + I",         move_local_rel(-1))

-- Local workspace numbers (1–9, 0 = 10) on CURRENT monitor
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

-- Mouse wheel → local workspaces
hl.bind(mainMod .. " + mouse_down",        focus_local_rel(1))
hl.bind(mainMod .. " + mouse_up",          focus_local_rel(-1))
hl.bind(mainMod .. " + CTRL + mouse_down", move_local_rel(1))
hl.bind(mainMod .. " + CTRL + mouse_up",   move_local_rel(-1))

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
local shotDir = "$HOME/Pictures/Screenshots"
local function shotName()
    return "\"Screenshot from $(date +'%Y-%m-%d %H-%M-%S').png\""
end

hl.bind("Print", hl.dsp.exec_cmd(
    "grim -g \"$(slurp)\" " .. shotDir .. "/" .. shotName()
), { description = "Screenshot region" })

hl.bind("CTRL + Print", hl.dsp.exec_cmd(
    "grim " .. shotDir .. "/" .. shotName()
), { description = "Screenshot full" })

hl.bind("ALT + Print", hl.dsp.exec_cmd(
    "grim -g \"$(hyprctl activewindow -j | jq -r '\"\\(.at[0]),\\(.at[1]) \\(.size[0])x\\(.size[1])\"')\" "
    .. shotDir .. "/" .. shotName()
), { description = "Screenshot active window" })

hl.bind(mainMod .. " + Print", hl.dsp.exec_cmd("noctalia msg screenshot-annotate"),
    { description = "Annotate screenshot" })

-- Session
hl.bind(mainMod .. " + SHIFT + E", hl.dsp.exec_cmd(
    "command -v hyprshutdown >/dev/null 2>&1 && hyprshutdown || hyprctl dispatch 'hl.dsp.exit()'"
), { description = "Exit Hyprland" })

hl.bind("CTRL + ALT + Delete", hl.dsp.exec_cmd(
    "command -v hyprshutdown >/dev/null 2>&1 && hyprshutdown || hyprctl dispatch 'hl.dsp.exit()'"
))

hl.bind(mainMod .. " + SHIFT + P", function()
    hl.timer(function()
        hl.dispatch(hl.dsp.dpms({ action = "off" }))
    end, { timeout = 300, type = "oneshot" })
end, { description = "Turn off displays" })

-- Media / brightness
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
hl.bind(mainMod .. " + mouse:272", hl.dsp.window.drag({}),
    { mouse = true })
hl.bind(mainMod .. " + mouse:273", hl.dsp.window.resize({ keep_aspect_ratio = false }),
    { mouse = true })
