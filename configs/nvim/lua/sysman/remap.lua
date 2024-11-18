vim.g.mapleader = " "

-- un-highlight
vim.keymap.set("n", "<Esc><Esc>", vim.cmd.nohlsearch, { desc = "Remove Current Highlights" })

-- Explore
vim.keymap.set("n", "<leader>pv", vim.cmd.Ex, { desc = "Explore Files" })

-- When highlighted, we can move the selected block up and down
vim.keymap.set("v", "K", ":m '<-2<CR>gv=gv", { desc = "Move selected block up" })
vim.keymap.set("v", "J", ":m '>+1<CR>gv=gv", { desc = "Move selected block down" })

-- Change the behaviour of J: don't move the cursor
vim.keymap.set("n", "J", "mzJ`z", { desc = "Don't move the cursor" })

-- vim.keymap.set("n", "Y", "yg$")

-- Centering the page
vim.keymap.set("n", "<C-d>", "<C-d>zz", { desc = "Page down" })
vim.keymap.set("n", "<C-u>", "<C-u>zz", { desc = "Page up" })
vim.keymap.set("n", "n", "nzzzv", { desc = "Next" })
vim.keymap.set("n", "N", "Nzzzv", { desc = "Prev" })

-- Preserving the yanked string, but paste it without yanking anything
vim.keymap.set("x", "<leader>p", "\"_dP")

-- Ynak into system's clipboard
vim.keymap.set("n", "<leader>y", "\"+y", { desc = "Copy to clipboard" })
vim.keymap.set("v", "<leader>y", "\"+y", { desc = "Copy to clipboard" })
vim.keymap.set("n", "<leader>Y", "\"+Y", { desc = "Copy to clipboard" })

vim.keymap.set("n", "<leader>d", "\"_d", { desc = "Delete" })
vim.keymap.set("v", "<leader>d", "\"_d", { desc = "Delete" })

-- Disabling Q
vim.keymap.set("n", "Q", "<nop>", { desc = "Disabled" })

-- Tmux
-- vim.keymap.set("n", "<C-f>", "<cmd>silent !tmux neww tmux-sessionizer<CR>")

-- formatting:
vim.keymap.set("n", "<M-S-l>", function()
    vim.lsp.buf.format()
end, { desc = "Reformat" })
vim.keymap.set("n", "<leader>bf", function()
    vim.lsp.buf.format()
end, { desc = "Reformat" })


-- vim.keymap.set("n", "<C-k>", "<cmd>cnext<CR>zz")
-- vim.keymap.set("n", "<C-j>", "<cmd>cprev<CR>zz")
-- vim.keymap.set("n", "<leader>k", "<cmd>lnext<CR>zz")
-- vim.keymap.set("n", "<leader>j", "<cmd>lprev<CR>zz")


-- Replace the thing under the cursor:
vim.keymap.set(
    "n",
    "<leader>r",
    ":%s/\\<<C-r><C-w>\\>/<C-r><C-w>/gI<Left><Left><Left>",
    { desc = "Replace the thing under the cursor" }
)

-- Increment and decrement numbers with Ctrl + Scroll
-- Increment more 10 Steps with Ctrl + Shift + Scroll
vim.api.nvim_set_keymap('n', '<C-ScrollWheelUp>', '<C-A>', { noremap = true, silent = true })
vim.api.nvim_set_keymap('n', '<C-ScrollWheelDown>', '<C-X>', { noremap = true, silent = true })
vim.api.nvim_set_keymap('n', '<C-S-ScrollWheelUp>', '10<C-A>', { noremap = true, silent = true })
vim.api.nvim_set_keymap('n', '<C-S-ScrollWheelDown>', '10<C-X>', { noremap = true, silent = true })


