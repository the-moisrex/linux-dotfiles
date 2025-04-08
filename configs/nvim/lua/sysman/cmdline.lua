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

local function put_selection_on_newline()

  local mode = vim.api.nvim_get_mode().mode
  -- Check for v (visual), V (visual line)
  if mode ~= 'v' and mode ~= 'V' then
    return
  end

  local api = vim.api
  local fn = vim.fn

  -- 1. Get visual selection boundaries (1-based)
  -- Using getpos is fine here. '< marks the start, '> marks the end.
  local start_pos = fn.getpos(".")
  local end_pos = fn.getpos("v")

  -- Extract 1-based line and column numbers
  local start_row, start_col = start_pos[2], start_pos[3]
  local end_row, end_col = end_pos[2], end_pos[3]
  local bufnr = api.nvim_get_current_buf() -- Use current buffer

  if start_row ~= end_row then
     vim.notify("Can't do multi-line cmd run yet.", vim.log.levels.INFO)
     return
  end
  if start_col > end_col then
      start_col, end_col = end_col, start_col
  end

  -- vim.notify("Selected text:" .. start_row .. " " .. start_col .. " " .. end_row .. " " .. end_col, vim.log.levels.INFO)

  -- 2. Get the precise selected text using nvim_buf_get_text
  -- This API requires 0-based indices.
  -- The end column for get_text is exclusive.
  local selected_lines_table = api.nvim_buf_get_text(
    bufnr,
    start_row - 1, -- 0-based start row
    start_col - 1, -- 0-based start col
    end_row - 1,   -- 0-based end row
    end_col,       -- 0-based end col (exclusive)
    {}             -- options table
  )

  -- Check if we actually got text
  if #selected_lines_table == 0 or (#selected_lines_table == 1 and selected_lines_table[1] == '') then
     vim.notify("Selected text is empty.", vim.log.levels.INFO)
     return
  end

  -- 3. Reconstruct the selected text, preserving internal newlines
  local selected_text = table.concat(selected_lines_table)
  -- vim.notify("Selected text:" .. selected_text, vim.log.levels.INFO)

  -- 4. Prepend the '$' sign
  local text_to_insert = "$ " .. selected_text


  -- 6. Insert the lines into the buffer *after* the original selection's end row.
  -- The API uses 0-based indices. To insert *after* 1-based end_row, use 0-based end_row.
  api.nvim_buf_set_lines(
    bufnr,
    end_row,  -- Start inserting AT this 0-based line index (effectively *after* the original end_row)
    end_row,  -- End index for replacement (same as start means pure insertion)
    false,    -- `strict_indexing = false`
    {text_to_insert} -- Pass the *table* of lines
  )

  -- 7. (Optional but recommended) Exit visual mode and move cursor
  api.nvim_feedkeys(api.nvim_replace_termcodes('<Esc>', true, false, true), 'n', false)
  -- Move cursor to the start of the first newly inserted line (1-based row, 0-based col)
  api.nvim_win_set_cursor(0, { end_row + 1, 0 }) -- 0 for current window


  -- Clear visual selection and return to normal mode
  -- vim.cmd("normal! <Esc>")

end


-- Main command execution function
local function run_cmd_under_cursor()
    if not vim.g.cmdrunner_enabled then
        vim.notify_once("CMDRunner is disabled for this filetype. Press <F9> to enable manually.", vim.log.levels.WARN, { title = "CMDRunner" })
        return
    end

    local bufnr = vim.api.nvim_get_current_buf()
    local mode = vim.fn.visualmode()
    local cmd
    local current_line_num = vim.api.nvim_win_get_cursor(0)[1]
    local start_insert_line -- 1-based line index where insertion should start
    start_insert_line = current_line_num + 1

    local current_line = vim.api.nvim_get_current_line()
    local cleaned_cmd = current_line:gsub("^%s*[$#>%s]*%s*", ""):gsub("%s*$", "")
    cmd = cleaned_cmd
    if not current_line:match("^%s*[$#>%s]") then
         vim.api.nvim_buf_set_lines(bufnr, current_line_num - 1, current_line_num, false, { "$ " .. cleaned_cmd })
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
end

local function run_selected_cmd()
  put_selection_on_newline()
  run_cmd_under_cursor()
end

-- Set up key mappings
local function setup_cmdrunner_mappings()
    if not vim.g.cmdrunner_enabled then
        vim.api.nvim_set_keymap('n', '<C-CR>', ':RunCMDUnderCursor<CR>', { noremap = true, silent = true, desc = "Run command under cursor" })
        vim.api.nvim_set_keymap('i', '<C-CR>', '<Esc>:RunCMDUnderCursor<CR>i', { noremap = true, silent = true, desc = "Run command under cursor" })
        -- vim.api.nvim_set_keymap('v', '<C-CR>', ':<C-u>RunCMDUnderCursor<CR>', { noremap = true, silent = true, desc = "Run selected command" })
        -- vim.api.nvim_set_keymap('x', '<C-CR>', ':PutSelectionOnNewLine<CR>:RunCMDUnderCursor<CR>', { noremap = true, silent = true, desc = "Run selected command" })
        vim.keymap.set('x', '<C-CR>', run_selected_cmd, { noremap = true, silent = true, desc = "Run selected command" })
        vim.g.cmdrunner_enabled = true -- Reflect that mappings are now active
    end
end

-- Remove key mappings
local function remove_cmdrunner_mappings()
    if vim.g.cmdrunner_enabled then
        pcall(vim.api.nvim_del_keymap, 'n', '<C-CR>')
        pcall(vim.api.nvim_del_keymap, 'i', '<C-CR>')
        pcall(vim.api.nvim_del_keymap, 'v', '<C-CR>')
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


vim.api.nvim_create_user_command('RunCMDUnderCursor', run_cmd_under_cursor, {})
vim.api.nvim_create_user_command('RunSelectedCMD', run_selected_cmd, {})

-- Toggle key mappings with F9
vim.api.nvim_set_keymap('n', '<F9>', ':lua _G.toggle_cmdrunner()<CR>', { noremap = true, silent = true, desc = "Toggle CMDRunner Mappings" })
vim.api.nvim_set_keymap('i', '<F9>', '<Esc>:lua _G.toggle_cmdrunner()<CR>i', { noremap = true, silent = true, desc = "Toggle CMDRunner Mappings" })
vim.api.nvim_set_keymap('v', '<F9>', ':<C-u>lua _G.toggle_cmdrunner()<CR>', { noremap = true, silent = true, desc = "Toggle CMDRunner Mappings" })

-- Initial check for the very first buffer opened
vim.defer_fn(update_cmdrunner_state_for_buffer, 100)
