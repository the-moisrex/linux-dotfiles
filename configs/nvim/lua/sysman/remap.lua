vim.g.mapleader = " "

-- Explore
vim.keymap.set("n", "<leader>pv", vim.cmd.Ex)

-- When highlighted, we can move the selected block up and down
vim.keymap.set("v", "J", ":m '>+1<CR>gv=gv")
vim.keymap.set("v", "K", ":m '<-2<CR>gv=gv")

-- Change the behaviour of J: don't move the cursor
vim.keymap.set("n", "J", "mzJ`z")

-- vim.keymap.set("n", "Y", "yg$")

-- Centering the page
vim.keymap.set("n", "<C-d>", "<C-d>zz")
vim.keymap.set("n", "<C-u>", "<C-u>zz")
vim.keymap.set("n", "n", "nzzzv")
vim.keymap.set("n", "N", "Nzzzv")

-- Preserving the yanked string, but paste it without yanking anything
vim.keymap.set("x", "<leader>p", "\"_dP")

-- Ynak into system's clipboard
vim.keymap.set("n", "<leader>y", "\"+y")
vim.keymap.set("v", "<leader>y", "\"+y")
vim.keymap.set("n", "<leader>Y", "\"+Y")

vim.keymap.set("n", "<leader>d", "\"_d")
vim.keymap.set("v", "<leader>d", "\"_d")

-- Disabling Q
vim.keymap.set("n", "Q", "<nop>")

-- Tmux
-- vim.keymap.set("n", "<C-f>", "<cmd>silent !tmux neww tmux-sessionizer<CR>")

-- formatting:
vim.keymap.set("n", "<M-S-l>", function()
    vim.lsp.buf.format()
end)
vim.keymap.set("n", "<leader>bf", function()
    vim.lsp.buf.format()
end)


-- vim.keymap.set("n", "<C-k>", "<cmd>cnext<CR>zz")
-- vim.keymap.set("n", "<C-j>", "<cmd>cprev<CR>zz")
-- vim.keymap.set("n", "<leader>k", "<cmd>lnext<CR>zz")
-- vim.keymap.set("n", "<leader>j", "<cmd>lprev<CR>zz")


-- Replace the thing under the cursor:
vim.keymap.set("n", "<leader>r", ":%s/\\<<C-r><C-w>\\>/<C-r><C-w>/gI<Left><Left><Left>")


