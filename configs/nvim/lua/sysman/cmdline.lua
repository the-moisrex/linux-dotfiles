-- Initialize plugin state
vim.g.cmdrunner_enabled = true
vim.g.cmdrunner_cwd = vim.fn.getcwd()  -- Start with Vim's current working directory

-- Set up key mappings
local function setup_cmdrunner_mappings()
    vim.api.nvim_set_keymap('n', '<C-CR>', ':RunCMDUnderCursor<CR>', { noremap = true, silent = true })
    vim.api.nvim_set_keymap('i', '<C-CR>', '<Esc>:RunCMDUnderCursor<CR>i', { noremap = true, silent = true })
    vim.api.nvim_set_keymap('v', '<C-CR>', ':<C-u>RunCMDUnderCursor<CR>', { noremap = true, silent = true })
end

local function remove_cmdrunner_mappings()
    pcall(vim.api.nvim_del_keymap, 'n', '<C-CR>')
    pcall(vim.api.nvim_del_keymap, 'i', '<C-CR>')
    pcall(vim.api.nvim_del_keymap, 'v', '<C-CR>')
end

-- Toggle the plugin
function _G.toggle_cmdrunner()
    if vim.g.cmdrunner_enabled then
        remove_cmdrunner_mappings()
        vim.g.cmdrunner_enabled = false
        print("CMDRunner: Disabled Ctrl+Enter mappings")
    else
        setup_cmdrunner_mappings()
        vim.g.cmdrunner_enabled = true
        print("CMDRunner: Enabled Ctrl+Enter mappings")
    end
end

-- Apply initial mappings if enabled
if vim.g.cmdrunner_enabled then
    setup_cmdrunner_mappings()
end

-- Remove ANSI color codes from output
local function remove_ansi_colors(line)
    return line:gsub("\27%[[%d;]*m", "")
end

-- Main command execution function
vim.api.nvim_create_user_command('RunCMDUnderCursor', function()
    local bufnr = vim.api.nvim_get_current_buf()
    local mode = vim.fn.visualmode()
    local cmd
    local insert_position

    -- Determine the command and insertion point based on mode
    if mode ~= '' then
        -- Visual mode: Use selected text as the command
        local start = vim.fn.getpos("'<")
        local end_ = vim.fn.getpos("'>")
        local lines = vim.api.nvim_buf_get_text(0, start[2] - 1, start[3] - 1, end_[2] - 1, end_[3], {})
        cmd = table.concat(lines, "\n")
        insert_position = end_[2] + 1  -- Insert after the selection
    else
        -- Normal mode: Clean and modify the current line
        local line_num = vim.api.nvim_win_get_cursor(0)[1]
        local current_line = vim.api.nvim_get_current_line()
        local cleaned_cmd = current_line:gsub("^%s*[$#]%s*", "")
        cmd = cleaned_cmd
        vim.api.nvim_buf_set_lines(bufnr, line_num - 1, line_num, false, { "$ " .. cleaned_cmd })
        insert_position = line_num + 1  -- Insert after the command line
    end

    -- Check if the command is a "cd" command (single-line only)
    local dir = cmd:match("^cd%s+(.*)$")
    if dir and not cmd:find("\n") then
        -- Handle "cd" by updating the working directory
        dir = dir:match("^%s*(.-)%s*$")  -- Trim leading/trailing spaces
        local full_path
        if dir == "" then
            -- "cd" with no argument goes to home directory
            full_path = vim.fn.expand('~')
        elseif dir:match("^/") then
            -- Absolute path
            full_path = dir
        elseif dir:match("^~") then
            -- Path starting with ~
            full_path = vim.fn.expand(dir)
        else
            -- Relative path, resolve against current cwd
            full_path = vim.fn.fnamemodify(vim.g.cmdrunner_cwd .. '/' .. dir, ':p')
        end

        -- Verify the directory exists before updating
        if vim.fn.isdirectory(full_path) == 1 then
            vim.g.cmdrunner_cwd = full_path
            local message = "Changed directory to " .. full_path
            vim.api.nvim_buf_set_lines(bufnr, insert_position - 1, insert_position - 1, false, { message })
            insert_position = insert_position + 1
            vim.api.nvim_buf_set_lines(bufnr, insert_position - 1, insert_position - 1, false, { "", "" })
            vim.api.nvim_win_set_cursor(0, { insert_position + 1, 0 })
        else
            local message = "Error: directory not found - " .. dir
            vim.api.nvim_buf_set_lines(bufnr, insert_position - 1, insert_position - 1, false, { message })
            insert_position = insert_position + 1
            vim.api.nvim_buf_set_lines(bufnr, insert_position - 1, insert_position - 1, false, { "", "" })
            vim.api.nvim_win_set_cursor(0, { insert_position + 1, 0 })
        end
    else
        -- Run non-"cd" commands with jobstart in the current cwd
        local job_id = vim.fn.jobstart(cmd, {
            cwd = vim.g.cmdrunner_cwd,  -- Use the tracked working directory
            stdout_buffered = false,
            stderr_buffered = false,
            on_stdout = function(_, data, _)
                vim.schedule(function()
                    for _, line in ipairs(data) do
                        local cleaned_line = remove_ansi_colors(line)
                        if cleaned_line ~= "" then
                            vim.api.nvim_buf_set_lines(bufnr, insert_position - 1, insert_position - 1, false, { cleaned_line })
                            insert_position = insert_position + 1
                        end
                    end
                end)
            end,
            on_stderr = function(_, data, _)
                vim.schedule(function()
                    for _, line in ipairs(data) do
                        local cleaned_line = remove_ansi_colors(line)
                        if cleaned_line ~= "" then
                            vim.api.nvim_buf_set_lines(bufnr, insert_position - 1, insert_position - 1, false, { "2: " .. cleaned_line })
                            insert_position = insert_position + 1
                        end
                    end
                end)
            end,
            on_exit = function(_, exit_code)
                vim.schedule(function()
                    vim.api.nvim_buf_set_lines(bufnr, insert_position - 1, insert_position - 1, false, { "", "" })
                    vim.api.nvim_win_set_cursor(0, { insert_position + 1, 0 })
                end)
            end
        })
    end
end, {})

-- Toggle key mappings with F9
vim.api.nvim_set_keymap('n', '<F9>', ':lua toggle_cmdrunner()<CR>', { noremap = true, silent = true })
vim.api.nvim_set_keymap('i', '<F9>', '<Esc>:lua toggle_cmdrunner()<CR>i', { noremap = true, silent = true })
vim.api.nvim_set_keymap('v', '<F9>', ':<C-u>lua toggle_cmdrunner()<CR>', { noremap = true, silent = true })
