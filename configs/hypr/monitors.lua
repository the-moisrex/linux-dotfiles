-- Auto-detect monitors — no personal info in git, everything detected at runtime.
-- Uses xrandr to find connected monitors and auto-arranges them left-to-right.

local function run_cmd(cmd)
  local handle = io.popen(cmd, "r")
  if not handle then return nil end
  local result = handle:read("*a")
  handle:close()
  return result
end

local function get_monitors()
  local monitors = {}
  local xr = run_cmd("xrandr --listmonitors 2>/dev/null")
  if not xr then return monitors end

  -- Parse lines like:  0: +*DP-1 2560/597x1440/336+1600+0  DP-1
  for line in xr:gmatch("[^\n]+") do
    local name, w, h, x, y = line:match("%d+: %+[*]?(%S+) (%d+)/%d+x(%d+)/%d+%+(%d+)%+(%d+)")
    if name then
      table.insert(monitors, {
        name = name,
        width = tonumber(w),
        height = tonumber(h),
        x = tonumber(x),
        y = tonumber(y),
      })
    end
  end

  -- Sort by x position (left to right)
  table.sort(monitors, function(a, b) return a.x < b.x end)

  return monitors
end

local function get_mode(output)
  local xr = run_cmd("xrandr 2>/dev/null")
  if not xr then return "preferred" end

  for line in xr:gmatch("[^\n]+") do
    if line:find(output .. " connected") then
      local mode_line = xr:match(output .. " connected[^\n]*\n%s+(%d+x%d+%S*)")
      if mode_line then
        local mode, refresh = mode_line:match("^(%d+x%d+)(@[%d.]+)?")
        if mode then
          return mode .. (refresh or "")
        end
      end
    end
  end

  return "preferred"
end

local monitors = get_monitors()

if #monitors > 0 then
  for _, mon in ipairs(monitors) do
    local mode = get_mode(mon.name)
    hl.monitor({
      output = mon.name,
      mode = mode,
      position = mon.x .. "x" .. mon.y,
      scale = 1,
    })
  end
else
  -- Fallback: let Hyprland auto-detect
  hl.monitor({ output = "", mode = "preferred", position = "auto", scale = 1 })
end
