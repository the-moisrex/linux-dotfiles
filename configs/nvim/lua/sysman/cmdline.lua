-- Initialize enable state
vim.g.cmdrunner_enabled = true

-- Configure key mappings
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

-- Toggle function
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

-- Initial mapping setup
if vim.g.cmdrunner_enabled then
    setup_cmdrunner_mappings()
end

-- Command definition
vim.api.nvim_create_user_command('RunCMDUnderCursor', function()
    local bufnr = vim.api.nvim_get_current_buf()
    local mode = vim.fn.visualmode()
    local cmd
    local insert_position

    if mode ~= '' then
        -- Visual mode: Use selected text without modification
        local start = vim.fn.getpos("'<")
        local end_ = vim.fn.getpos("'>")
        local lines = vim.api.nvim_buf_get_text(0, start[2] - 1, start[3] - 1, end_[2] - 1, end_[3], {})
        cmd = table.concat(lines, "\n")
        insert_position = end_[2]
    else
        -- Normal mode: Clean up and modify the current line
        local line_num = vim.api.nvim_win_get_cursor(0)[1]
        local current_line = vim.api.nvim_get_current_line()
        local cleaned_cmd = current_line:gsub("^%s*[$#]%s*", "")
        cmd = cleaned_cmd
        -- Modify the current line by prepending "$ " to the cleaned command
        local new_line = "$ " .. cleaned_cmd
        vim.api.nvim_buf_set_lines(bufnr, line_num - 1, line_num, false, { new_line })
        insert_position = line_num
    end

    local job_id = vim.fn.jobstart(cmd, {
        stdout_buffered = false,
        on_stdout = function(_, data, _)
            vim.schedule(function()
                for _, line in ipairs(data) do
                    if line ~= "" then
                        vim.api.nvim_buf_set_lines(bufnr, insert_position, insert_position, false, { line })
                        insert_position = insert_position + 1
                    end
                end
            end)
        end,
        on_exit = function(_, exit_code)
            vim.schedule(function()
                vim.api.nvim_buf_set_lines(bufnr, insert_position, insert_position, false, { "", "" })
                vim.api.nvim_win_set_cursor(0, { insert_position + 2, 0 })
            end)
        end
    })
end, {})

-- Toggle mappings
vim.api.nvim_set_keymap('n', '<F9>', ':lua toggle_cmdrunner()<CR>', { noremap = true, silent = true })
vim.api.nvim_set_keymap('i', '<F9>', '<Esc>:lua toggle_cmdrunner()<CR>i', { noremap = true, silent = true })
vim.api.nvim_set_keymap('v', '<F9>', ':<C-u>lua toggle_cmdrunner()<CR>', { noremap = true, silent = true })
