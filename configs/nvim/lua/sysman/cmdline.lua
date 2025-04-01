--[[
    CMDRunner: Executes the command under the cursor or visual selection
    within Neovim, displaying the output inline.
    Handles 'cd' commands internally to manage the working directory.
    Mappings can be toggled with <F9>.
    Enabled by default only for specific filetypes.
]]

-- Define extensions where cmdrunner should be enabled by default
-- "" represents files with no extension.
local enabled_extensions = { "", "txt", "md" }

-- Helper function to check if a value exists in a table (array-like)
local function contains(tbl, val)
  for _, v in ipairs(tbl) do
    if v == val then
      return true
    end
  end
  return false
end

-- Initialize plugin state - Start disabled, let autocmd enable if needed
vim.g.cmdrunner_enabled = false -- Start disabled
vim.g.cmdrunner_cwd = vim.fn.getcwd()  -- Start with Vim's current working directory

-- Store mappings state separate from the global enabled flag for clarity
local mappings_active = false

-- Set up key mappings
local function setup_cmdrunner_mappings()
    if not mappings_active then
        vim.api.nvim_set_keymap('n', '<C-CR>', ':RunCMDUnderCursor<CR>', { noremap = true, silent = true, desc = "Run command under cursor" })
        vim.api.nvim_set_keymap('i', '<C-CR>', '<Esc>:RunCMDUnderCursor<CR>i', { noremap = true, silent = true, desc = "Run command under cursor" })
        vim.api.nvim_set_keymap('v', '<C-CR>', ':<C-u>RunCMDUnderCursor<CR>', { noremap = true, silent = true, desc = "Run selected command" })
        mappings_active = true
        vim.g.cmdrunner_enabled = true -- Reflect that mappings are now active
    end
end

-- Remove key mappings
local function remove_cmdrunner_mappings()
    if mappings_active then
        pcall(vim.api.nvim_del_keymap, 'n', '<C-CR>')
        pcall(vim.api.nvim_del_keymap, 'i', '<C-CR>')
        pcall(vim.api.nvim_del_keymap, 'v', '<C-CR>')
        mappings_active = false
        vim.g.cmdrunner_enabled = false -- Reflect that mappings are now inactive
    end
end

-- Function to check filetype and set state accordingly
local function update_cmdrunner_state_for_buffer()
    local bufnr = vim.api.nvim_get_current_buf()
    if not vim.api.nvim_buf_is_loaded(bufnr) or vim.api.nvim_buf_get_option(bufnr, 'buftype') ~= '' then
        if vim.g.cmdrunner_enabled then
             remove_cmdrunner_mappings()
        end
        return
    end

    local current_extension = vim.fn.expand('%:e')
    if contains(enabled_extensions, current_extension) then
        setup_cmdrunner_mappings()
    else
        remove_cmdrunner_mappings()
    end
end

-- Autocommand to update state when entering a buffer
local cmdrunner_augroup = vim.api.nvim_create_augroup('CmdRunnerState', { clear = true })
vim.api.nvim_create_autocmd('BufEnter', {
    group = cmdrunner_augroup,
    pattern = '*',
    callback = update_cmdrunner_state_for_buffer,
})

-- Manual toggle function
function _G.toggle_cmdrunner()
    if vim.g.cmdrunner_enabled then
        remove_cmdrunner_mappings()
        vim.notify("CMDRunner: Manually Disabled Ctrl+Enter mappings", vim.log.levels.INFO)
    else
        setup_cmdrunner_mappings()
        vim.notify("CMDRunner: Manually Enabled Ctrl+Enter mappings", vim.log.levels.INFO)
    end
end

-- Remove ANSI color codes from output
local function remove_ansi_colors(line)
    if type(line) ~= "string" then return "" end
    return line:gsub("\27%[[%d;]*m", "")
end

-- Main command execution function
vim.api.nvim_create_user_command('RunCMDUnderCursor', function()
    if not vim.g.cmdrunner_enabled then
        vim.notify_once("CMDRunner is disabled for this filetype. Press <F9> to enable manually.", vim.log.levels.WARN, { title = "CMDRunner" })
        return
    end

    local bufnr = vim.api.nvim_get_current_buf()
    local mode = vim.fn.visualmode()
    local cmd
    local start_insert_line -- 1-based line index where insertion should start

    -- Determine the command and insertion point based on mode (Logic mostly unchanged)
    if mode ~= '' then
        local start_pos = vim.fn.getpos("'<")
        local end_pos = vim.fn.getpos("'>")
        local v_start_line, v_start_col, v_end_line, v_end_col
        if start_pos[2] > end_pos[2] or (start_pos[2] == end_pos[2] and start_pos[3] > end_pos[3]) then
            v_start_line, v_start_col = end_pos[2], end_pos[3]
            v_end_line, v_end_col = start_pos[2], start_pos[3]
        else
            v_start_line, v_start_col = start_pos[2], start_pos[3]
            v_end_line, v_end_col = end_pos[2], end_pos[3]
        end
        local lines = vim.api.nvim_buf_get_lines(bufnr, v_start_line - 1, v_end_line, false)
        if #lines == 1 then
            if v_end_col >= v_start_col then
                lines[1] = lines[1]:sub(v_start_col, v_end_col)
            else lines[1] = "" end
        else
             if v_end_col > 0 and v_end_col <= #lines[#lines] then lines[#lines] = lines[#lines]:sub(1, v_end_col) end
             if v_start_col > 1 and v_start_col <= #lines[1] then lines[1] = lines[1]:sub(v_start_col) end
        end
        cmd = table.concat(lines, "\n")
        start_insert_line = v_end_line + 1
    else
        local current_line_num = vim.api.nvim_win_get_cursor(0)[1]
        local current_line = vim.api.nvim_get_current_line()
        local cleaned_cmd = current_line:gsub("^%s*[$#>%s]*%s*", ""):gsub("%s*$", "")
        cmd = cleaned_cmd
        if not current_line:match("^%s*[$#>%s]") then
             vim.api.nvim_buf_set_lines(bufnr, current_line_num - 1, current_line_num, false, { "$ " .. cleaned_cmd })
        end
        start_insert_line = current_line_num + 1
    end

    cmd = cmd:match("^%s*(.-)%s*$")
    if cmd == "" then
        vim.notify("CMDRunner: No command to run.", vim.log.levels.WARN)
        return
    end

    local is_cd_command = false
    local dir_argument = nil
    local match_data = { cmd:match("^[cC][dD]%s+(.+)$") }
    if match_data[1] then
        is_cd_command = true
        dir_argument = match_data[1]:match("^%s*(.-)%s*$")
    elseif cmd:lower() == "cd" then
        is_cd_command = true
        dir_argument = ""
    end

    -- Handle 'cd' command separately (no streaming needed)
    if is_cd_command and not cmd:find("\n") then
        local full_path
        local dir = dir_argument
        if dir == "" or dir == "~" then full_path = vim.fn.expand('~')
        elseif dir == "-" then
            full_path = nil
            vim.notify("CMDRunner: 'cd -' is not yet supported.", vim.log.levels.WARN)
        elseif dir:match("^/") then full_path = dir
        elseif dir:match("^~") then full_path = vim.fn.expand(dir)
        else
             if vim.fs and vim.fs.normalize then full_path = vim.fs.normalize(vim.g.cmdrunner_cwd .. '/' .. dir)
             else
                 full_path = vim.fn.fnamemodify(vim.g.cmdrunner_cwd .. '/' .. dir, ':p')
                 full_path = full_path:gsub('/+', '/'):gsub('/%.?$', ''):gsub('//', '/')
             end
        end

        local message
        if type(full_path) == "string" and full_path ~= "" then
            local stat = vim.loop.fs_stat(full_path)
            if stat and stat.type == "directory" then
                vim.g.cmdrunner_cwd = full_path
                message = "PWD: " .. full_path -- Use PWD: as requested
            elseif stat then message = "CMDRunner Error: Not a directory - " .. full_path
            else message = "CMDRunner Error: Path not found - " .. full_path end
        elseif full_path == nil then message = "CMDRunner Error: Could not determine target directory for '" .. cmd .. "'"
        else message = "CMDRunner Error: Invalid path calculated for '" .. dir .. "'" end

        local start_insert_idx = start_insert_line - 1
        -- Insert message and TWO blank lines as requested
        local lines_to_insert = { message, "", "" }
        vim.api.nvim_buf_set_lines(bufnr, start_insert_idx, start_insert_idx, false, lines_to_insert)

        -- Position cursor on the second blank line
        local final_cursor_line = start_insert_idx + #lines_to_insert + 1 -- Index (0-based) + num_lines + 1 = 1-based line num
        local max_lines = vim.api.nvim_buf_line_count(bufnr)
        final_cursor_line = math.min(final_cursor_line, max_lines)
        vim.api.nvim_win_set_cursor(0, { final_cursor_line, 0 })

    else
        -- Run non-"cd" commands with streaming output
        local start_insert_idx = start_insert_line - 1 -- Initial 0-indexed insertion point
        -- This variable will track the next insertion index, shared across callbacks via closure
        local current_insert_idx = start_insert_idx

        vim.fn.jobstart(cmd, {
            cwd = vim.g.cmdrunner_cwd,
            -- *** Set buffered to false for streaming ***
            stdout_buffered = false,
            stderr_buffered = false,
            on_stdout = function(_, data, _)
                -- Data is usually a table of lines when buffered=false
                if data then
                    local lines_to_insert_now = {}
                    for _, line in ipairs(data) do
                        local cleaned_line = remove_ansi_colors(line)
                        cleaned_line = cleaned_line:gsub("\r$", "") -- Remove trailing CR
                        -- Only insert non-empty lines (behavior from previous version)
                        if cleaned_line ~= "" then
                           table.insert(lines_to_insert_now, cleaned_line)
                        end
                    end

                    if #lines_to_insert_now > 0 then
                         vim.schedule(function()
                             if not vim.api.nvim_buf_is_valid(bufnr) then return end
                             -- Clamp insert index just in case buffer changed drastically
                             local safe_insert_idx = math.min(current_insert_idx, vim.api.nvim_buf_line_count(bufnr))
                             vim.api.nvim_buf_set_lines(bufnr, safe_insert_idx, safe_insert_idx, false, lines_to_insert_now)
                             -- Update the index for the *next* insertion
                             current_insert_idx = safe_insert_idx + #lines_to_insert_now
                         end)
                     end
                end
            end,
            on_stderr = function(_, data, _)
                 if data then
                    local lines_to_insert_now = {}
                    for _, line in ipairs(data) do
                        local cleaned_line = remove_ansi_colors(line)
                        cleaned_line = cleaned_line:gsub("\r$", "") -- Remove trailing CR
                        if cleaned_line ~= "" then
                            -- Prepend stderr marker
                            table.insert(lines_to_insert_now, "stderr: " .. cleaned_line)
                        end
                    end

                    if #lines_to_insert_now > 0 then
                        vim.schedule(function()
                            if not vim.api.nvim_buf_is_valid(bufnr) then return end
                            local safe_insert_idx = math.min(current_insert_idx, vim.api.nvim_buf_line_count(bufnr))
                            vim.api.nvim_buf_set_lines(bufnr, safe_insert_idx, safe_insert_idx, false, lines_to_insert_now)
                            -- Update the index for the *next* insertion
                            current_insert_idx = safe_insert_idx + #lines_to_insert_now
                        end)
                    end
                 end
            end,
            on_exit = function(_, exit_code, _)
                vim.schedule(function()
                    if not vim.api.nvim_buf_is_valid(bufnr) then return end

                    -- *** Insert TWO final blank lines as requested ***
                    local lines_to_insert = { "", "" }
                    local safe_insert_idx = math.min(current_insert_idx, vim.api.nvim_buf_line_count(bufnr))
                    vim.api.nvim_buf_set_lines(bufnr, safe_insert_idx, safe_insert_idx, false, lines_to_insert)

                    -- Calculate cursor pos based on where the *blank lines* were inserted
                    -- Place cursor on the second blank line
                    local final_cursor_line = safe_insert_idx + #lines_to_insert + 1

                    local new_max_lines = vim.api.nvim_buf_line_count(bufnr)
                    final_cursor_line = math.min(final_cursor_line, new_max_lines)

                    vim.api.nvim_win_set_cursor(0, { final_cursor_line, 0 })
                end)
            end,
            pty = true, -- Keep PTY for better interactive-like behavior
        })
    end
end, {})

-- Toggle key mappings with F9
vim.api.nvim_set_keymap('n', '<F9>', ':lua _G.toggle_cmdrunner()<CR>', { noremap = true, silent = true, desc = "Toggle CMDRunner Mappings" })
vim.api.nvim_set_keymap('i', '<F9>', '<Esc>:lua _G.toggle_cmdrunner()<CR>i', { noremap = true, silent = true, desc = "Toggle CMDRunner Mappings" })
vim.api.nvim_set_keymap('v', '<F9>', ':<C-u>lua _G.toggle_cmdrunner()<CR>', { noremap = true, silent = true, desc = "Toggle CMDRunner Mappings" })

-- Initial check for the very first buffer opened
vim.defer_fn(update_cmdrunner_state_for_buffer, 100)
