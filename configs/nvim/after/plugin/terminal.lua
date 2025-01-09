
local function is_buffer_visible(buf_number)
    -- Get all windows
    local windows = vim.api.nvim_list_wins()
    
    -- Iterate through each window
    for _, win in ipairs(windows) do
        -- Get the buffer associated with the window
        local win_buf = vim.api.nvim_win_get_buf(win)
        if win_buf == buf_number then
            return true  -- Buffer is visible in this window
        end
    end
    
    return false  -- Buffer is not visible in any window
end

function auto_split()
    local win_id = vim.api.nvim_get_current_win()
    local win_width = vim.api.nvim_win_get_width(win_id)
    local win_height = vim.api.nvim_win_get_height(win_id)

    if win_width > win_height then
        vim.cmd('botright vsplit')
    else
        vim.cmd('botright split')
    end
end


function toggle_terminal()
    local buffers = vim.api.nvim_list_bufs()
    local terminal_buf = nil

    for _, buf in ipairs(buffers) do
        if vim.api.nvim_buf_get_option(buf, 'buftype') == 'terminal' then
            terminal_buf = buf
            break
        end
    end

    if terminal_buf then
        if vim.api.nvim_buf_is_loaded(terminal_buf) and is_buffer_visible(terminal_buf) then
            vim.cmd('close' .. terminal_buf)
        else
            -- vim.cmd('botright split')
            auto_split()
            vim.cmd('buffer ' .. terminal_buf)
            vim.fn.feedkeys('a') -- Switch to insert mode in terminal
        end
    else
        -- vim.cmd('botright split')
        auto_split()
        vim.cmd('terminal')
        vim.fn.feedkeys('a') -- Switch to insert mode in terminal
    end
end

vim.api.nvim_set_keymap('n', '<F12>', ':lua toggle_terminal()<CR>', { noremap = true, silent = true })
vim.api.nvim_set_keymap('t', '<F12>', '<ESC><C-\\><C-n>:lua toggle_terminal()<CR>', { noremap = true, silent = true })


-- Open Terminal
vim.keymap.set("n", "<S-t>T", function()
    auto_split()
    vim.cmd('terminal')
    vim.fn.feedkeys('a') -- Switch to insert mode in terminal
end)
vim.keymap.set("n", "<S-t>\"", function()
    vim.cmd('botright split')
    vim.cmd('terminal')
    vim.fn.feedkeys('a') -- Switch to insert mode in terminal
end)
vim.keymap.set("n", "<S-t>%", function()
    vim.cmd('botright vsplit')
    vim.cmd('terminal')
    vim.fn.feedkeys('a') -- Switch to insert mode in terminal
end)

