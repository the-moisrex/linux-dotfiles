
local function is_buffer_visible(buf_number)
    -- Get a list of window IDs that show the specified buffer
    local windows = vim.fn.win_findbuf(buf_number)
    -- Check if the list is not empty
    return #windows > 0
end

-- local function is_terminal_buffer(buf_number)
--     -- Get the term_title variable for the buffer
--     local term_title = vim.fn.getbufvar(buf_number, 'term_title')
--     -- Check if term_title is set (not empty)
--     return term_title ~= ''
-- end

local function is_terminal_buffer(buf_number)
    -- Get the buffer type using getbufvar
    local buftype = vim.fn.getbufvar(buf_number, "&buftype")
    -- Check if the buffer type is 'terminal'
    return buftype == 'terminal'
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
        if is_terminal_buffer(buf) then
            terminal_buf = buf
            break
        end
    end

    if terminal_buf then
        if vim.api.nvim_buf_is_loaded(terminal_buf) and is_buffer_visible(terminal_buf) then
            vim.cmd('close ' .. terminal_buf)
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

vim.keymap.set('n', '<F12>', toggle_terminal, { noremap = true, silent = true })
vim.keymap.set('t', '<F12>', function() 
    vim.api.nvim_win_hide(vim.api.nvim_get_current_win())
end, { noremap = true, silent = true })


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

