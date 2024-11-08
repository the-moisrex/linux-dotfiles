local popup = require("plenary.popup")

local function open_popup_and_run_command()
    -- Create the popup window
    local opts = {
        title = "Command Input",
        border = {},
        minwidth = 70,
        minheight = 20,
        maxheight = 80,
        padding = { 2, 2, 2, 2 },
    }

    -- Create a buffer for the popup
    local buf = vim.api.nvim_create_buf(false, true)

    -- Create the popup window
    local win_id = popup.create(buf, opts)

    -- Set up input prompt in the buffer
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "test-unicode-algos" })

    -- Function to handle input and execute command
    local function handle_input()
        local input = vim.api.nvim_get_current_line()

        -- Construct and run the command
        local command = string.format("run %s", input)
        local output = vim.fn.system(command)  -- Capture command output

        vim.api.nvim_buf_set_lines(buf, 0, -1, false, vim.split(output, "\n"))
    end

    local function handle_close()
        vim.api.nvim_win_close(win_id, true)
    end

    -- Map Enter key to handle input
    vim.api.nvim_buf_set_keymap(buf, 'n', '<CR>', '', {
        callback = handle_input,
        noremap = true,
        silent = true,
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', 'q', '', {
        callback = handle_close,
        noremap = true,
        silent = true,
    })
end

-- Map a key to trigger the function (e.g., <F5>)
vim.keymap.set('n', '<F5>', open_popup_and_run_command, { silent = true })


